#include "nirievents.h"

#include <QJsonArray>
#include <QJsonObject>

#include <QJSEngine>
#include <QCoreApplication>
#include <QPointer>
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

NiriEvents::NiriEvents(QObject *parent, NiriEvents *source)
    : NiriIpcEvents(parent), m_source(source)
{
    if (source) {
        forwardEventSignals(source);
        connect(source, &NiriEvents::rawEvent, this, &NiriEvents::rawEvent);
        connect(source, &NiriEvents::decodeError, this, &NiriEvents::decodeError);
        connect(source, &NiriEvents::unknownEvent, this, &NiriEvents::unknownEvent);
        return;
    }
    NiriConnection::instance();
    connect(NiriSession::instance(), &NiriSession::eventPublication, this,
            [this](const NiriWire::Document &document, quint64 producingEpoch) {
                dispatchDocument(document, producingEpoch);
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

void NiriEvents::dispatchWireEvent(const QString &name, const NiriWire::WireValue &payload,
                                   quint64 token)
{
    NiriWire::Document document;
    document.root = *NiriWire::WireValue::object({{name, payload}});
    dispatchDocument(document, token);
}

void NiriEvents::dispatchDocument(const NiriWire::Document &document, quint64 token)
{
    if (!NiriSession::checkThread(this))
        return;
    if (token != NiriSession::instance()->epoch())
        return;
    if (m_source) {
        m_source->dispatchDocument(document, token);
        return;
    }
    const auto &wire = document.root;
    QString name;
    const NiriWire::WireValue *payload = &wire;
    if (wire.stringValue())
        name = *wire.stringValue();
    else if (wire.objectValue() && wire.objectValue()->size() == 1) {
        name = wire.objectValue()->first().first;
        payload = &wire.objectValue()->first().second;
    }
    qCDebug(niriEventsLog) << "Event" << name;
    const auto raw = NiriWire::toQmlProjection(*payload);
    QPointer<NiriEvents> guard(this);
    const auto current = [guard, token] {
        return guard && NiriSession::instance()->epoch() == token;
    };
    NiriEvent event;
    NiriIpc::DecodeError detail;
    if (!NiriIpc::decodeEvent(wire, event, detail)) {
        emit decodeError(detail.path, detail.message, raw);
        if (current())
            emit rawEvent(name, raw);
        return;
    }
    if (!event.known()) {
        emit unknownEvent(name, raw);
        if (current())
            emit rawEvent(name, raw);
        return;
    }
    // Decode once, commit current transitional snapshots, then project this same
    // native value to both buses. E7 replaces these snapshots with its reducer.
    applyExistingState(event);
    publishEvent(event, current);
    if (current())
        emit rawEvent(name, raw);
}

void NiriEvents::applyExistingState(const NiriEvent &event)
{
#define PAYLOAD(Tag) const auto &payload = std::get<NiriEvent::Tag##Value>(event.value).payload
    switch (event.kind()) {
    case NiriEvent::WorkspacesChanged: {
        PAYLOAD(WorkspacesChanged);
        m_lastWorkspaces = NiriIpc::qmlValue(payload.workspaces).toList();
        break;
    }
    case NiriEvent::WindowsChanged: {
        PAYLOAD(WindowsChanged);
        m_lastWindows = NiriIpc::qmlValue(payload.windows).toList();
        m_lastFocusedWindowId.reset();
        for (const QVariant &v : m_lastWindows) {
            const auto w = v.value<NiriWindow>();
            if (w.isFocused) {
                m_lastFocusedWindowId = w.id;
                break;
            }
        }
        break;
    }
    case NiriEvent::WindowOpenedOrChanged: {
        PAYLOAD(WindowOpenedOrChanged);
        const auto &w = payload.window;
        if (w.isFocused)
            m_lastFocusedWindowId = w.id;
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
        break;
    }
    case NiriEvent::WindowClosed: {
        PAYLOAD(WindowClosed);
        const auto id = payload.id;
        for (int i = 0; i < m_lastWindows.size(); ++i) {
            if (m_lastWindows[i].value<NiriWindow>().id == id) {
                m_lastWindows.removeAt(i);
                break;
            }
        }
        if (m_lastFocusedWindowId == id)
            m_lastFocusedWindowId.reset();
        break;
    }
    case NiriEvent::WindowFocusChanged: {
        PAYLOAD(WindowFocusChanged);
        const auto id = payload.id;
        m_lastFocusedWindowId = id;
        for (int i = 0; i < m_lastWindows.size(); ++i) {
            NiriWindow w = m_lastWindows[i].value<NiriWindow>();
            bool nowFocused = id && w.id == *id;
            if (w.isFocused != nowFocused) {
                w.isFocused = nowFocused;
                m_lastWindows[i] = QVariant::fromValue(w);
            }
        }
        break;
    }
    case NiriEvent::KeyboardLayoutsChanged: {
        PAYLOAD(KeyboardLayoutsChanged);
        m_lastKeyboardLayouts = payload.keyboardLayouts;
        break;
    }
    case NiriEvent::KeyboardLayoutSwitched: {
        PAYLOAD(KeyboardLayoutSwitched);
        m_lastKeyboardLayouts.currentIdx = payload.idx;
        break;
    }
    case NiriEvent::OverviewOpenedOrClosed: {
        PAYLOAD(OverviewOpenedOrClosed);
        m_lastOverviewOpen = payload.isOpen;
        break;
    }
    case NiriEvent::WorkspaceActivated: {
        PAYLOAD(WorkspaceActivated);
        const auto id = payload.id;
        const auto focused = payload.focused;
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
        break;
    }
    case NiriEvent::WorkspaceActiveWindowChanged: {
        PAYLOAD(WorkspaceActiveWindowChanged);
        const auto workspaceId = payload.workspaceId;
        const auto windowId = payload.activeWindowId;
        for (auto &value : m_lastWorkspaces) {
            auto workspace = value.value<NiriWorkspace>();
            if (workspace.id == workspaceId) {
                workspace.activeWindowId = windowId;
                value = QVariant::fromValue(workspace);
                break;
            }
        }
        break;
    }
    default:
        break;
    }
#undef PAYLOAD
}
