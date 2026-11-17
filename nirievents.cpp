#include "nirievents.h"

#include <QJsonArray>
#include <QJsonObject>

#include <QJSEngine>
#include <QCoreApplication>
#include <optional>

#include "niriconnection.h"
#include "generated/niriipccodec_generated.h"
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
        FORWARD(castStartedOrChanged);
        FORWARD(windowFocusTimestampChanged);
        FORWARD(rawEvent);
        FORWARD(decodeError);
#undef FORWARD
        return;
    }
    NiriConnection::instance();
    connect(NiriSession::instance(), &NiriSession::eventPublication, this,
            [this](const NiriWire::Document &document, quint64 producingEpoch) {
                const auto *object = document.root.objectValue();
                if (object && object->size() == 1)
                    dispatchWireEvent(object->first().first, object->first().second,
                                      producingEpoch);
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
std::optional<quint64> NiriEvents::lastFocusedWindowId() const
{
    if (!NiriSession::checkThread(this))
        return std::nullopt;
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
    const auto wire = NiriWire::fromQVariantLossy(payload);
    if (wire)
        dispatchWireEvent(name, *wire, token);
}

namespace {
template <typename T>
bool eventField(const NiriWire::WireValue &payload, const QString &key, T &value,
                NiriIpc::DecodeError &error, const QString &path)
{
    const auto *wire = payload.lookup(key);
    if (!wire)
        return NiriIpc::fail(error, NiriIpc::childPath(path, key), QStringLiteral("missing field"));
    return NiriIpc::decode(*wire, value, &error, NiriIpc::childPath(path, key));
}
template <typename T>
bool eventField(const NiriWire::WireValue &payload, const QString &key, std::optional<T> &value,
                NiriIpc::DecodeError &error, const QString &path)
{
    const auto *wire = payload.lookup(key);
    if (!wire) {
        value.reset();
        return true;
    }
    return NiriIpc::decode(*wire, value, &error, NiriIpc::childPath(path, key));
}
} // namespace

void NiriEvents::dispatchWireEvent(const QString &name, const NiriWire::WireValue &payload,
                                   quint64 token)
{
    if (!NiriSession::checkThread(this))
        return;
    if (token != NiriSession::instance()->epoch())
        return;
    if (m_source) {
        m_source->dispatchWireEvent(name, payload, token);
        return;
    }
    qCDebug(niriEventsLog) << "Event" << name;
    const auto raw = NiriWire::toQmlProjection(payload).toMap();
    const auto path = NiriIpc::childPath(QStringLiteral("$"), name);
    NiriIpc::DecodeError detail;
    auto reject = [&] {
        emit decodeError(detail.path, detail.message, raw);
        if (NiriSession::instance()->epoch() == token)
            emit rawEvent(name, raw);
    };
    // Event bodies are records, including empty and all-optional payloads.
    // Check the container once, before lookup can confuse non-objects with
    // absent optional fields, and before any state or typed publication.
    if (!payload.objectValue()) {
        NiriIpc::fail(detail, path, QStringLiteral("expected event payload object"));
        reject();
        return;
    }
#define FIELD(T, V, K)                                              \
    T V{};                                                          \
    if (!eventField(payload, QStringLiteral(K), V, detail, path)) { \
        reject();                                                   \
        return;                                                     \
    }

    if (name == QStringLiteral("WorkspacesChanged")) {
        FIELD(QList<NiriWorkspace>, records, "workspaces");
        m_lastWorkspaces = NiriIpc::qmlValue(records).toList();
        emit workspacesChanged(m_lastWorkspaces);
    } else if (name == QStringLiteral("WindowsChanged")) {
        FIELD(QList<NiriWindow>, records, "windows");
        m_lastWindows = NiriIpc::qmlValue(records).toList();
        m_lastFocusedWindowId.reset();
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
        FIELD(NiriWindow, w, "window");
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
        FIELD(quint64, id, "id");
        for (int i = 0; i < m_lastWindows.size(); ++i) {
            if (m_lastWindows[i].value<NiriWindow>().id == id) {
                m_lastWindows.removeAt(i);
                break;
            }
        }
        if (m_lastFocusedWindowId == id)
            m_lastFocusedWindowId.reset();
        emit m_native.windowClosed(id);
        if (NiriSession::instance()->epoch() == token)
            emit windowClosed(QString::number(id));
    } else if (name == QStringLiteral("WindowFocusChanged")) {
        FIELD(std::optional<quint64>, id, "id");
        m_lastFocusedWindowId = id;
        // Update isFocused flag in cached snapshot so late subscribers
        // that bootstrap from lastWindowsSnapshot() see accurate focus state.
        for (int i = 0; i < m_lastWindows.size(); ++i) {
            NiriWindow w = m_lastWindows[i].value<NiriWindow>();
            bool nowFocused = id && w.id == *id;
            if (w.isFocused != nowFocused) {
                w.isFocused = nowFocused;
                m_lastWindows[i] = QVariant::fromValue(w);
            }
        }
        emit m_native.windowFocusChanged(id);
        if (NiriSession::instance()->epoch() == token)
            emit windowFocusChanged(NiriIpc::qmlValue(id));
    } else if (name == QStringLiteral("KeyboardLayoutsChanged")) {
        FIELD(NiriKeyboardLayouts, layouts, "keyboard_layouts");
        m_lastKeyboardLayouts = layouts;
        emit keyboardLayoutsChanged(m_lastKeyboardLayouts);
    } else if (name == QStringLiteral("KeyboardLayoutSwitched")) {
        FIELD(quint8, idx, "idx");
        m_lastKeyboardLayouts.currentIdx = idx;
        emit keyboardLayoutSwitched(idx);
    } else if (name == QStringLiteral("OverviewOpenedOrClosed")) {
        FIELD(bool, isOpen, "is_open");
        m_lastOverviewOpen = isOpen;
        emit overviewOpenedOrClosed(m_lastOverviewOpen);
    } else if (name == QStringLiteral("ConfigLoaded")) {
        FIELD(bool, failed, "failed");
        emit configLoaded(failed);
    } else if (name == QStringLiteral("CastsChanged")) {
        FIELD(QList<NiriCast>, casts, "casts");
        emit castsChanged(NiriIpc::qmlValue(casts).toList());
    } else if (name == QStringLiteral("CastStartedOrChanged")) {
        FIELD(NiriCast, cast, "cast");
        emit castStartedOrChanged(cast);
    } else if (name == QStringLiteral("WindowFocusTimestampChanged")) {
        FIELD(quint64, id, "id");
        FIELD(std::optional<NiriTimestamp>, timestamp, "focus_timestamp");
        emit m_native.windowFocusTimestampChanged(id, timestamp);
        if (NiriSession::instance()->epoch() == token)
            emit windowFocusTimestampChanged(QString::number(id), NiriIpc::qmlValue(timestamp));
    } else if (name == QStringLiteral("WorkspaceActivated")) {
        FIELD(quint64, id, "id");
        FIELD(bool, focused, "focused");
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
        emit m_native.workspaceActivated(id, focused);
        if (NiriSession::instance()->epoch() == token)
            emit workspaceActivated(QString::number(id), focused);
    } else if (name == QStringLiteral("WorkspaceActiveWindowChanged")) {
        FIELD(quint64, workspaceId, "workspace_id");
        FIELD(std::optional<quint64>, windowId, "active_window_id");
        for (auto &value : m_lastWorkspaces) {
            auto workspace = value.value<NiriWorkspace>();
            if (workspace.id == workspaceId) {
                workspace.activeWindowId = windowId;
                value = QVariant::fromValue(workspace);
                break;
            }
        }
        emit m_native.workspaceActiveWindowChanged(workspaceId, windowId);
        if (NiriSession::instance()->epoch() == token)
            emit workspaceActiveWindowChanged(QString::number(workspaceId),
                                              NiriIpc::qmlValue(windowId));
    } else if (name == QStringLiteral("WindowLayoutsChanged")) {
        FIELD(QList<NiriWindowLayoutChange>, changes, "changes");
        emit windowLayoutsChanged(NiriIpc::qmlValue(changes).toList());
    }

    if (NiriSession::instance()->epoch() == token)
        emit rawEvent(name, raw);
#undef FIELD
}
