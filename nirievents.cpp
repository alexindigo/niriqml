#include "nirievents.h"

#include <QJsonArray>
#include <QJsonObject>

#include <QJSEngine>

#include "niriconnection.h"
#include "niriutils.h"

NiriEvents *NiriEvents::create(QQmlEngine *, QJSEngine *jsEngine)
{
    NiriEvents *self = instance();
    if (jsEngine)
        QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    NiriConnection *conn = NiriConnection::instance();
    if (!conn->isConnected() && conn->socketPath().isEmpty())
        conn->connectToSocket();
    return self;
}

NiriEvents *NiriEvents::instance()
{
    static NiriEvents s;
    return &s;
}

NiriEvents::NiriEvents(QObject *parent) : QObject(parent)
{
    connect(NiriConnection::instance(), &NiriConnection::eventReceived, this,
            &NiriEvents::dispatchEvent);
}

QVariantList NiriEvents::lastWindowsSnapshot() const
{
    return m_lastWindows;
}
QVariantList NiriEvents::lastWorkspacesSnapshot() const
{
    return m_lastWorkspaces;
}
NiriKeyboardLayouts NiriEvents::lastKeyboardLayouts() const
{
    return m_lastKeyboardLayouts;
}
bool NiriEvents::lastOverviewOpen() const
{
    return m_lastOverviewOpen;
}
quint64 NiriEvents::lastFocusedWindowId() const
{
    return m_lastFocusedWindowId;
}

void NiriEvents::dispatchEvent(const QString &name, const QVariantMap &payload)
{
    qDebug() << "NiriEvents:" << name << "(payload keys:" << payload.keys() << ")";
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
        quint64 id = NIRI_INT("id");
        if (id != 0) {
            m_lastFocusedWindowId = id;
            // Update isFocused flag in cached snapshot so late subscribers
            // that bootstrap from lastWindowsSnapshot() see accurate focus state.
            for (int i = 0; i < m_lastWindows.size(); ++i) {
                NiriWindow w = m_lastWindows[i].value<NiriWindow>();
                bool nowFocused = (w.id == id);
                if (w.isFocused != nowFocused) {
                    w.isFocused = nowFocused;
                    m_lastWindows[i] = QVariant::fromValue(w);
                }
            }
            emit windowFocusChanged(id);
        }
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
        emit workspaceActivated(NIRI_INT("id"), NIRI_BOOL("focused"));
    } else if (name == QStringLiteral("WorkspaceActiveWindowChanged")) {
        emit workspaceActiveWindowChanged(NIRI_INT("workspace_id"), NIRI_INT("active_window_id"));
    } else if (name == QStringLiteral("WindowLayoutsChanged")) {
        emit windowLayoutsChanged(NIRI_ARR("changes").toVariantList());
    }

    emit rawEvent(name, payload);
}
