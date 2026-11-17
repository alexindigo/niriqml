#include "niristate.h"

#include <QJSEngine>
#include <QCoreApplication>
#include "nirisession.h"

#include "nirievents.h"

NiriState *NiriState::create(QQmlEngine *engine, QJSEngine *jsEngine)
{
    if (!NiriSession::checkThread(engine ? engine : jsEngine))
        return nullptr;
    QObject *owner = engine ? static_cast<QObject *>(engine) : static_cast<QObject *>(jsEngine);
    if (!owner)
        return instance();
    auto *self = new NiriState(owner);
    QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    NiriSession::instance()->ensureStarted();
    return self;
}

NiriState *NiriState::instance()
{
    if (!NiriSession::checkThread())
        return nullptr;
    auto *app = QCoreApplication::instance();
    auto *self = app->findChild<NiriState *>(QStringLiteral("niriqml.state"),
                                             Qt::FindDirectChildrenOnly);
    if (!self) {
        self = new NiriState(app);
        self->setObjectName(QStringLiteral("niriqml.state"));
    }
    NiriSession::instance()->ensureStarted();
    return self;
}

NiriState::NiriState(QObject *parent) : QObject(parent)
{
    NiriEvents *events = NiriEvents::instance();
    connect(events, &NiriEvents::windowsChanged, this, &NiriState::onWindowsChanged);
    connect(events, &NiriEvents::windowOpenedOrChanged, this, &NiriState::onWindowOpenedOrChanged);
    connect(events->nativeEvents(), &NiriNativeEvents::windowClosed, this,
            &NiriState::onWindowClosed);
    connect(events->nativeEvents(), &NiriNativeEvents::windowFocusChanged, this,
            &NiriState::onWindowFocusChanged);
    connect(events, &NiriEvents::workspacesChanged, this, &NiriState::onWorkspacesChanged);
    connect(events->nativeEvents(), &NiriNativeEvents::workspaceActivated, this,
            &NiriState::onWorkspaceActivated);

    // Bootstrap from cached snapshots for late instantiation
    QVariantList windows = events->lastWindowsSnapshot();
    if (!windows.isEmpty())
        onWindowsChanged(windows);
    QVariantList workspaces = events->lastWorkspacesSnapshot();
    if (!workspaces.isEmpty())
        onWorkspacesChanged(workspaces);
    NiriSession::instance()->ensureStarted();
}

std::optional<quint64> NiriState::focusedWindowId() const
{
    return NiriSession::checkThread(this) ? m_focusedWindowId : std::nullopt;
}
std::optional<QString> NiriState::focusedWindowAppId() const
{
    return NiriSession::checkThread(this) ? m_focusedWindowAppId : std::nullopt;
}
std::optional<QString> NiriState::focusedWindowTitle() const
{
    return NiriSession::checkThread(this) ? m_focusedWindowTitle : std::nullopt;
}
std::optional<quint64> NiriState::focusedWorkspaceId() const
{
    return NiriSession::checkThread(this) ? m_focusedWorkspaceId : std::nullopt;
}
std::optional<QString> NiriState::activeOutput() const
{
    return NiriSession::checkThread(this) ? m_activeOutput : std::nullopt;
}
int NiriState::windowCount() const
{
    return NiriSession::checkThread(this) ? m_windowCount : 0;
}
int NiriState::workspaceCount() const
{
    return NiriSession::checkThread(this) ? m_workspaceCount : 0;
}

void NiriState::syncFocusedWindowInfo()
{
    auto it = m_focusedWindowId ? m_windowsById.constFind(*m_focusedWindowId)
                                : m_windowsById.constEnd();
    if (it != m_windowsById.constEnd()) {
        m_focusedWindowAppId = it->appId;
        m_focusedWindowTitle = it->title;
    } else {
        m_focusedWindowAppId.reset();
        m_focusedWindowTitle.reset();
    }
}

void NiriState::onWindowsChanged(const QVariantList &windows)
{
    m_windowsById.clear();
    std::optional<quint64> newFocusedId;
    for (const QVariant &v : windows) {
        NiriWindow w = v.value<NiriWindow>();
        m_windowsById[w.id] = w;
        if (w.isFocused)
            newFocusedId = w.id;
    }
    m_windowCount = m_windowsById.size();
    m_focusedWindowId = newFocusedId;
    syncFocusedWindowInfo();
    emit stateChanged();
}

void NiriState::onWindowOpenedOrChanged(const NiriWindow &window)
{
    bool wasNew = !m_windowsById.contains(window.id);
    m_windowsById[window.id] = window;
    if (wasNew)
        m_windowCount = m_windowsById.size();

    if (window.isFocused && m_focusedWindowId != window.id)
        m_focusedWindowId = window.id;

    if (window.id == m_focusedWindowId)
        syncFocusedWindowInfo();

    emit stateChanged();
}

void NiriState::onWindowClosed(quint64 id)
{
    if (!m_windowsById.contains(id))
        return;
    m_windowsById.remove(id);
    m_windowCount = m_windowsById.size();

    if (m_focusedWindowId == id) {
        m_focusedWindowId.reset();
        m_focusedWindowAppId.reset();
        m_focusedWindowTitle.reset();
    }
    emit stateChanged();
}

void NiriState::onWindowFocusChanged(std::optional<quint64> id)
{
    if (m_focusedWindowId == id)
        return;
    m_focusedWindowId = id;
    syncFocusedWindowInfo();
    emit stateChanged();
}

void NiriState::onWorkspacesChanged(const QVariantList &workspaces)
{
    m_workspacesById.clear();
    std::optional<quint64> newFocused;
    std::optional<QString> newActiveOutput;
    for (const QVariant &v : workspaces) {
        NiriWorkspace ws = v.value<NiriWorkspace>();
        m_workspacesById[ws.id] = ws;
        if (ws.isFocused) {
            newFocused = ws.id;
            newActiveOutput = ws.output;
        }
    }
    m_workspaceCount = m_workspacesById.size();
    m_focusedWorkspaceId = newFocused;
    m_activeOutput = newActiveOutput;
    emit stateChanged();
}

void NiriState::onWorkspaceActivated(quint64 workspaceId, bool focused)
{
    // Update our cached workspace: mark this one active (and previously-active
    // one inactive) on the same output.
    auto it = m_workspacesById.find(workspaceId);
    if (it == m_workspacesById.end())
        return;
    const auto output = it->output;
    for (auto wit = m_workspacesById.begin(); wit != m_workspacesById.end(); ++wit) {
        if (wit->output == output)
            wit->isActive = (wit->id == workspaceId);
    }
    it->isActive = true;

    if (focused) {
        m_focusedWorkspaceId = workspaceId;
        m_activeOutput = output;
    }
    emit stateChanged();
}
