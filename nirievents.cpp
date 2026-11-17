#include "nirievents.h"

#include <QCoreApplication>
#include <QPointer>
#include "nirisession.h"
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
    connect(NiriSession::instance(), &NiriSession::decodedEventPublication, this,
            &NiriEvents::publishDecoded);
}

QVariantList NiriEvents::lastWindowsSnapshot() const
{
    return NiriSession::checkThread(this)
            ? NiriIpc::qmlValue(NiriSession::instance()->state().windowsSnapshot()).toList()
            : QVariantList{};
}
QVariantList NiriEvents::lastWorkspacesSnapshot() const
{
    return NiriSession::checkThread(this)
            ? NiriIpc::qmlValue(NiriSession::instance()->state().workspacesSnapshot()).toList()
            : QVariantList{};
}
std::optional<NiriKeyboardLayouts> NiriEvents::lastKeyboardLayouts() const
{
    return NiriSession::checkThread(this) ? NiriSession::instance()->state().keyboardLayouts()
                                          : std::nullopt;
}
QVariant NiriEvents::qmlLastKeyboardLayouts() const
{
    return NiriSession::checkThread(this)
            ? NiriIpc::qmlValue(NiriSession::instance()->state().keyboardLayouts())
            : QVariant::fromValue(nullptr);
}
bool NiriEvents::lastOverviewOpen() const
{
    return NiriSession::checkThread(this) && NiriSession::instance()->state().overviewOpen();
}
std::optional<quint64> NiriEvents::lastFocusedWindowId() const
{
    return NiriSession::checkThread(this) ? NiriSession::instance()->state().focusedWindowId()
                                          : std::nullopt;
}
QVariant NiriEvents::qmlLastFocusedWindowId() const
{
    return NiriIpc::qmlValue(lastFocusedWindowId());
}
QVariantList NiriEvents::lastCastsSnapshot() const
{
    return NiriSession::checkThread(this)
            ? NiriIpc::qmlValue(NiriSession::instance()->state().castsSnapshot()).toList()
            : QVariantList{};
}
bool NiriEvents::lastConfigFailed() const
{
    return NiriSession::checkThread(this) && NiriSession::instance()->state().configFailed();
}

void NiriEvents::dispatchEvent(const QString &name, const QVariantMap &payload)
{
    if (NiriSession::checkThread(this))
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
    if (!NiriSession::checkThread(this) || token != NiriSession::instance()->epoch())
        return;
    if (m_source) {
        m_source->dispatchDocument(document, token);
        return;
    }
    // Explicit synthetic publication adapter, never a production state writer.
    // State tests feed a test socket/session or an independent native reducer.
    NiriEvent event;
    NiriIpc::DecodeError detail;
    NiriIpc::decodeEvent(document.root, event, detail);
    publishDecoded(document, event, detail.path, detail.message, token);
}

void NiriEvents::publishDecoded(const NiriWire::Document &document, const NiriEvent &event,
                                const QString &errorPath, const QString &errorMessage,
                                quint64 token)
{
    if (token != NiriSession::instance()->epoch())
        return;
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
    if (!errorMessage.isEmpty()) {
        emit decodeError(errorPath, errorMessage, raw);
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
    publishEvent(event, current);
    if (current())
        emit rawEvent(name, raw);
}
