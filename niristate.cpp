#include "niristate.h"

#include <QJSEngine>

#include "nirievents.h"

NiriState *NiriState::create(QQmlEngine *, QJSEngine *jsEngine)
{
    NiriState *self = instance();
    if (jsEngine)
        QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    return self;
}

NiriState *NiriState::instance()
{
    static NiriState s;
    return &s;
}

NiriState::NiriState(QObject *parent) : QObject(parent)
{
    NiriEvents *events = NiriEvents::instance();
    connect(events, &NiriEvents::windowsChanged, this, &NiriState::onWindowsChanged);
    connect(events, &NiriEvents::windowOpenedOrChanged, this, &NiriState::onWindowOpenedOrChanged);
    connect(events, &NiriEvents::windowClosed, this, &NiriState::onWindowClosed);
    connect(events, &NiriEvents::windowFocusChanged, this, &NiriState::onWindowFocusChanged);
    connect(events, &NiriEvents::workspacesChanged, this, &NiriState::onWorkspacesChanged);
    connect(events, &NiriEvents::workspaceActivated, this, &NiriState::onWorkspaceActivated);

    // Bootstrap from cached snapshots for late instantiation
    QVariantList windows = events->lastWindowsSnapshot();
    if (!windows.isEmpty())
        onWindowsChanged(windows);
    QVariantList workspaces = events->lastWorkspacesSnapshot();
    if (!workspaces.isEmpty())
        onWorkspacesChanged(workspaces);
}

quint64 NiriState::focusedWindowId() const
{
    return m_focusedWindowId;
}
QString NiriState::focusedWindowAppId() const
{
    return m_focusedWindowAppId;
}
QString NiriState::focusedWindowTitle() const
{
    return m_focusedWindowTitle;
}
quint64 NiriState::focusedWorkspaceId() const
{
    return m_focusedWorkspaceId;
}
QString NiriState::activeOutput() const
{
    return m_activeOutput;
}
int NiriState::windowCount() const
{
    return m_windowCount;
}
int NiriState::workspaceCount() const
{
    return m_workspaceCount;
}

void NiriState::syncFocusedWindowInfo()
{
    auto it = m_windowsById.constFind(m_focusedWindowId);
    if (it != m_windowsById.constEnd()) {
        m_focusedWindowAppId = it->appId;
        m_focusedWindowTitle = it->title;
    } else {
        m_focusedWindowAppId.clear();
        m_focusedWindowTitle.clear();
    }
}

void NiriState::onWindowsChanged(const QVariantList &windows)
{
    m_windowsById.clear();
    quint64 newFocusedId = 0;
    for (const QVariant &v : windows) {
        NiriWindow w = v.value<NiriWindow>();
        m_windowsById[w.id] = w;
        if (w.isFocused)
            newFocusedId = w.id;
    }
    m_windowCount = m_windowsById.size();
    if (newFocusedId != 0)
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
        m_focusedWindowId = 0;
        m_focusedWindowAppId.clear();
        m_focusedWindowTitle.clear();
    }
    emit stateChanged();
}

void NiriState::onWindowFocusChanged(quint64 id)
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
    quint64 newFocused = 0;
    QString newActiveOutput;
    for (const QVariant &v : workspaces) {
        NiriWorkspace ws = v.value<NiriWorkspace>();
        m_workspacesById[ws.id] = ws;
        if (ws.isFocused) {
            newFocused = ws.id;
            newActiveOutput = ws.output;
        }
    }
    m_workspaceCount = m_workspacesById.size();
    if (newFocused != 0) {
        m_focusedWorkspaceId = newFocused;
        m_activeOutput = newActiveOutput;
    }
    emit stateChanged();
}

void NiriState::onWorkspaceActivated(quint64 workspaceId, bool focused)
{
    // Update our cached workspace: mark this one active (and previously-active
    // one inactive) on the same output.
    auto it = m_workspacesById.find(workspaceId);
    if (it == m_workspacesById.end())
        return;
    const QString output = it->output;
    for (auto wit = m_workspacesById.begin(); wit != m_workspacesById.end(); ++wit) {
        if (wit->output == output)
            wit->isActive = (wit->id == workspaceId);
    }
    it->isActive = true;

    if (focused) {
        m_focusedWorkspaceId = workspaceId;
        if (!output.isEmpty())
            m_activeOutput = output;
    }
    emit stateChanged();
}
