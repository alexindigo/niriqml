#include "nirievents.h"

#include <QJsonArray>
#include <QJsonObject>

#include <QJSEngine>
#include <QCoreApplication>
#include <optional>

#include "niriconnection.h"
#include "niriutils.h"
#include "nirilogging.h"

NiriEvents *NiriEvents::create(QQmlEngine *engine, QJSEngine *jsEngine)
{
    if (!NiriSession::checkThread(engine ? engine : jsEngine))
        return nullptr;
    QObject *owner = engine ? static_cast<QObject *>(engine) : static_cast<QObject *>(jsEngine);
    if (!owner)
        return instance();
    auto *self = new NiriEvents(owner, instance());
    QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    NiriSession::instance()->ensureStarted();
    return self;
}

NiriEvents *NiriEvents::instance()
{
    if (!NiriSession::checkThread())
        return nullptr;
    auto *app = QCoreApplication::instance();
    auto *self = app->findChild<NiriEvents *>(QStringLiteral("niriqml.events"),
                                              Qt::FindDirectChildrenOnly);
    if (!self) {
        self = new NiriEvents(app);
        self->setObjectName(QStringLiteral("niriqml.events"));
    }
    NiriSession::instance()->ensureStarted();
    return self;
}

NiriEvents::NiriEvents(QObject *parent, NiriEvents *source) : QObject(parent), m_source(source)
{
    if (source) {
#define FORWARD(name) connect(source, &NiriEvents::name, this, &NiriEvents::name)
        FORWARD(workspacesChanged);
        FORWARD(workspaceActivated);
        FORWARD(workspaceActiveWindowChanged);
        FORWARD(windowsChanged);
        FORWARD(windowOpenedOrChanged);
        FORWARD(windowClosed);
        FORWARD(windowFocusChanged);
        FORWARD(windowLayoutsChanged);
        FORWARD(keyboardLayoutsChanged);
        FORWARD(keyboardLayoutSwitched);
        FORWARD(overviewOpenedOrClosed);
        FORWARD(configLoaded);
        FORWARD(castsChanged);
        FORWARD(windowFocusTimestampChanged);
        FORWARD(rawEvent);
#undef FORWARD
        return;
    }
    NiriConnection::instance();
    connect(NiriSession::instance(), &NiriSession::eventReceived, this,
            [this](const QString &name, const QVariantMap &payload, quint64 producingEpoch) {
                dispatchEvent(name, payload, producingEpoch);
            });
}

QVariantList NiriEvents::lastWindowsSnapshot() const
{
    if (!NiriSession::checkThread(this))
        return {};
    return m_source ? m_source->lastWindowsSnapshot() : m_lastWindows;
}
QVariantList NiriEvents::lastWorkspacesSnapshot() const
{
    if (!NiriSession::checkThread(this))
        return {};
    return m_source ? m_source->lastWorkspacesSnapshot() : m_lastWorkspaces;
}
NiriKeyboardLayouts NiriEvents::lastKeyboardLayouts() const
{
    if (!NiriSession::checkThread(this))
        return {};
    return m_source ? m_source->lastKeyboardLayouts() : m_lastKeyboardLayouts;
}
bool NiriEvents::lastOverviewOpen() const
{
    if (!NiriSession::checkThread(this))
        return false;
    return m_source ? m_source->lastOverviewOpen() : m_lastOverviewOpen;
}
quint64 NiriEvents::lastFocusedWindowId() const
{
    if (!NiriSession::checkThread(this))
        return 0;
    return m_source ? m_source->lastFocusedWindowId() : m_lastFocusedWindowId;
}

void NiriEvents::dispatchEvent(const QString &name, const QVariantMap &payload)
{
    if (!NiriSession::checkThread(this))
        return;
    // Explicit synthetic C++ injection. Real transport always supplies its
    // producing epoch, never one recaptured after consumer callbacks.
    dispatchEvent(name, payload, NiriSession::instance()->epoch());
}

void NiriEvents::dispatchEvent(const QString &name, const QVariantMap &payload, quint64 token)
{
    if (!NiriSession::checkThread(this))
        return;
    if (token != NiriSession::instance()->epoch())
        return;
    if (m_source) {
        m_source->dispatchEvent(name, payload, token);
        return;
    }
    qCDebug(niriEventsLog) << "Event" << name;
    QJsonObject obj = QJsonObject::fromVariantMap(payload);

#define NIRI_ARR(K) obj.value(QStringLiteral(K)).toArray()
#define NIRI_INT(K) obj.value(QStringLiteral(K)).toInteger()
#define NIRI_BOOL(K) obj.value(QStringLiteral(K)).toBool()
#define NIRI_GADGET(K, T) fromNiriJson<T>(obj.value(QStringLiteral(K)).toObject())
#define NIRI_LIST(K, T) \
    jsonArrayToGadgets(obj.value(QStringLiteral(K)).toArray(), QMetaType::fromType<T>())

    if (name == QStringLiteral("WorkspacesChanged")) {
        m_lastWorkspaces = NIRI_LIST("workspaces", NiriWorkspace);
        emit workspacesChanged(m_lastWorkspaces);
    } else if (name == QStringLiteral("WindowsChanged")) {
        m_lastWindows = NIRI_LIST("windows", NiriWindow);
        // Update focus tracking from snapshot
        for (const QVariant &v : m_lastWindows) {
            NiriWindow w = v.value<NiriWindow>();
            if (w.isFocused) {
                m_lastFocusedWindowId = w.id;
                break;
            }
        }
        emit windowsChanged(m_lastWindows);
    } else if (name == QStringLiteral("WindowOpenedOrChanged")) {
        NiriWindow w = NIRI_GADGET("window", NiriWindow);
        if (w.isFocused)
            m_lastFocusedWindowId = w.id;
        // Also update the cached snapshot
        bool found = false;
        for (int i = 0; i < m_lastWindows.size(); ++i) {
            if (m_lastWindows[i].value<NiriWindow>().id == w.id) {
                m_lastWindows[i] = QVariant::fromValue(w);
                found = true;
                break;
            }
        }
        if (!found)
            m_lastWindows.append(QVariant::fromValue(w));
        emit windowOpenedOrChanged(w);
    } else if (name == QStringLiteral("WindowClosed")) {
        quint64 id = NIRI_INT("id");
        for (int i = 0; i < m_lastWindows.size(); ++i) {
            if (m_lastWindows[i].value<NiriWindow>().id == id) {
                m_lastWindows.removeAt(i);
                break;
            }
        }
        if (m_lastFocusedWindowId == id)
            m_lastFocusedWindowId = 0;
        emit windowClosed(id);
    } else if (name == QStringLiteral("WindowFocusChanged")) {
        // id is Option<u64>: null becomes 0 here and means "nothing is
        // focused anymore" (e.g. switched to an empty workspace). Don't
        // drop it — consumers must clear their focused state, otherwise the
        // last focused window looks focused forever.
        quint64 id = NIRI_INT("id");
        m_lastFocusedWindowId = id;
        // Update isFocused flag in cached snapshot so late subscribers
        // that bootstrap from lastWindowsSnapshot() see accurate focus state.
        for (int i = 0; i < m_lastWindows.size(); ++i) {
            NiriWindow w = m_lastWindows[i].value<NiriWindow>();
            bool nowFocused = (id != 0 && w.id == id);
            if (w.isFocused != nowFocused) {
                w.isFocused = nowFocused;
                m_lastWindows[i] = QVariant::fromValue(w);
            }
        }
        emit windowFocusChanged(id);
    } else if (name == QStringLiteral("KeyboardLayoutsChanged")) {
        m_lastKeyboardLayouts = NIRI_GADGET("keyboard_layouts", NiriKeyboardLayouts);
        emit keyboardLayoutsChanged(m_lastKeyboardLayouts);
    } else if (name == QStringLiteral("KeyboardLayoutSwitched")) {
        int idx = int(NIRI_INT("idx"));
        m_lastKeyboardLayouts.currentIdx = idx;
        emit keyboardLayoutSwitched(idx);
    } else if (name == QStringLiteral("OverviewOpenedOrClosed")) {
        m_lastOverviewOpen = NIRI_BOOL("is_open");
        emit overviewOpenedOrClosed(m_lastOverviewOpen);
    } else if (name == QStringLiteral("ConfigLoaded")) {
        emit configLoaded(NIRI_BOOL("failed"));
    } else if (name == QStringLiteral("CastsChanged")) {
        emit castsChanged(NIRI_ARR("casts").toVariantList());
    } else if (name == QStringLiteral("WindowFocusTimestampChanged")) {
        emit windowFocusTimestampChanged(NIRI_INT("id"),
                                         NIRI_GADGET("focus_timestamp", NiriTimestamp));
    } else if (name == QStringLiteral("WorkspaceActivated")) {
        const quint64 id = NIRI_INT("id");
        const bool focused = NIRI_BOOL("focused");
        std::optional<QString> output;
        for (const auto &value : m_lastWorkspaces) {
            const auto workspace = value.value<NiriWorkspace>();
            if (workspace.id == id) {
                output = workspace.output;
                break;
            }
        }
        if (output) {
            for (auto &value : m_lastWorkspaces) {
                auto workspace = value.value<NiriWorkspace>();
                if (workspace.output == *output)
                    workspace.isActive = workspace.id == id;
                if (focused)
                    workspace.isFocused = workspace.id == id;
                value = QVariant::fromValue(workspace);
            }
        }
        emit workspaceActivated(id, focused);
    } else if (name == QStringLiteral("WorkspaceActiveWindowChanged")) {
        const quint64 workspaceId = NIRI_INT("workspace_id");
        const quint64 windowId = NIRI_INT("active_window_id");
        for (auto &value : m_lastWorkspaces) {
            auto workspace = value.value<NiriWorkspace>();
            if (workspace.id == workspaceId) {
                workspace.activeWindowId = windowId;
                value = QVariant::fromValue(workspace);
                break;
            }
        }
        emit workspaceActiveWindowChanged(workspaceId, windowId);
    } else if (name == QStringLiteral("WindowLayoutsChanged")) {
        emit windowLayoutsChanged(NIRI_ARR("changes").toVariantList());
    }

    if (NiriSession::instance()->epoch() == token)
        emit rawEvent(name, payload);
}
