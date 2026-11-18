#include "niriconnection.h"
#include <QCoreApplication>

NiriConnection *NiriConnection::create(QQmlEngine *engine, QJSEngine *jsEngine)
{
    if (!NiriSession::checkThread(engine ? engine : jsEngine))
        return nullptr;
    QObject *owner = engine ? static_cast<QObject *>(engine) : static_cast<QObject *>(jsEngine);
    if (!owner)
        return instance();
    auto *self = new NiriConnection(owner);
    QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    self->m_session->ensureStarted();
    return self;
}

NiriConnection *NiriConnection::instance()
{
    if (!NiriSession::checkThread())
        return nullptr;
    auto *app = QCoreApplication::instance();
    auto *self = app->findChild<NiriConnection *>(QStringLiteral("niriqml.connection"),
                                                  Qt::FindDirectChildrenOnly);
    if (!self) {
        self = new NiriConnection(app);
        self->setObjectName(QStringLiteral("niriqml.connection"));
    }
    self->m_session->ensureStarted();
    return self;
}

NiriConnection::NiriConnection(QObject *parent)
    : QObject(parent), m_session(NiriSession::instance())
{
#define FORWARD(name) connect(m_session, &NiriSession::name, this, &NiriConnection::name)
    FORWARD(connectedChanged);
    FORWARD(socketPathChanged);
    FORWARD(endpointSourceChanged);
    FORWARD(statusChanged);
    FORWARD(subscribedChanged);
    FORWARD(autoReconnectChanged);
    FORWARD(errorChanged);
    FORWARD(peerInfoChanged);
    FORWARD(reconnectScheduled);
    // Public compatibility signal omits epoch, but the internal typed event
    // receiver subscribes to the session's producing-epoch signal directly.
    FORWARD(eventReceived);
#undef FORWARD
}

bool NiriConnection::isConnected() const
{
    return NiriSession::checkThread(this) && m_session->isConnected();
}
QString NiriConnection::socketPath() const
{
    return NiriSession::checkThread(this) ? m_session->currentEndpoint().path : QString{};
}
QString NiriConnection::errorString() const
{
    return NiriSession::checkThread(this) ? m_session->errorString() : QString{};
}
QVariantMap NiriConnection::error() const
{
    return NiriSession::checkThread(this) ? m_session->error() : QVariantMap{};
}
PeerInfo NiriConnection::peerInfo() const
{
    return NiriSession::checkThread(this) ? m_session->peerInfo() : PeerInfo{};
}
NiriConnection::Status NiriConnection::status() const
{
    return NiriSession::checkThread(this) ? static_cast<Status>(m_session->status()) : Failed;
}
NiriConnection::EndpointSource NiriConnection::endpointSource() const
{
    return NiriSession::checkThread(this)
            ? static_cast<EndpointSource>(m_session->currentEndpoint().source)
            : Environment;
}
bool NiriConnection::subscribed() const
{
    return NiriSession::checkThread(this) && m_session->subscribed();
}
bool NiriConnection::autoReconnect() const
{
    return NiriSession::checkThread(this) && m_session->autoReconnect();
}

void NiriConnection::connectToSocket(const QString &path)
{
    if (!NiriSession::checkThread(this))
        return;
    m_session->connectToSocket(path);
}
void NiriConnection::disconnect()
{
    stop();
}
void NiriConnection::start()
{
    if (NiriSession::checkThread(this))
        m_session->start();
}
void NiriConnection::stop()
{
    if (NiriSession::checkThread(this))
        m_session->stop();
}
void NiriConnection::selectEndpoint(const QString &path)
{
    if (NiriSession::checkThread(this))
        m_session->selectEndpoint(path);
}
void NiriConnection::useEnvironmentEndpoint()
{
    if (NiriSession::checkThread(this))
        m_session->useEnvironmentEndpoint();
}
void NiriConnection::setAutoReconnect(bool enabled)
{
    if (NiriSession::checkThread(this))
        m_session->setAutoReconnect(enabled);
}

void NiriConnection::setMaxFrameBytes(qsizetype bytes)
{
    if (NiriSession::checkThread(this))
        m_session->setMaxFrameBytes(bytes);
}

qsizetype NiriConnection::maxFrameBytes() const
{
    return NiriSession::checkThread(this) ? m_session->maxFrameBytes() : 0;
}
