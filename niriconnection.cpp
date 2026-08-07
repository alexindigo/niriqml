#include "niriconnection.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QJSEngine>
#include <QDebug>
#include <QVector>

#ifdef Q_OS_LINUX
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <sys/un.h>
#endif

NiriConnection *NiriConnection::create(QQmlEngine *, QJSEngine *jsEngine)
{
    NiriConnection *self = instance();
    if (jsEngine)
        QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    if (!self->m_connected && self->m_socketPath.isEmpty())
        self->connectToSocket();
    return self;
}

NiriConnection *NiriConnection::instance()
{
    static NiriConnection s;
    return &s;
}

NiriConnection::NiriConnection(QObject *parent) : QObject(parent)
{
    connect(&m_socket, &QLocalSocket::connected, this, &NiriConnection::onConnected);
    connect(&m_socket, &QLocalSocket::disconnected, this, &NiriConnection::onDisconnected);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, &NiriConnection::onErrorOccurred);
    connect(&m_socket, &QLocalSocket::readyRead, this, &NiriConnection::onReadyRead);

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (m_socketPath.isEmpty())
            return;
        qInfo() << "NiriConnection: reconnect attempt" << m_reconnectAttempt;
        connectToSocket(m_socketPath);
    });
}

NiriConnection::~NiriConnection()
{
    // Static singleton: destroyed during process teardown, when Qt's event
    // dispatcher may already be gone. ~QLocalSocket would attempt a graceful
    // disconnectFromHost(), which starts a QTimer and segfaults. Close the
    // socket immediately (no timers) and stop the reconnect timer first.
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        m_socket.abort();
}

PeerInfo NiriConnection::peerInfo() const
{
    PeerInfo info;
#ifdef Q_OS_LINUX
    if (!m_connected)
        return info;

    struct ucred cred;
    socklen_t len = sizeof(cred);
    if (getsockopt(m_socket.socketDescriptor(), SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
        info.pid = static_cast<qint64>(cred.pid);
        info.uid = static_cast<quint32>(cred.uid);
        info.gid = static_cast<quint32>(cred.gid);
    }

    struct sockaddr_un addr;
    len = sizeof(addr);
    if (getsockopt(m_socket.socketDescriptor(), SOL_SOCKET, SO_PEERNAME, &addr, &len) == 0
        && addr.sun_path[0] != '\0') {
        info.peerSocketPath = QString::fromUtf8(addr.sun_path);
    }

    // SO_PEERSEC requires two calls: first to get the buffer length,
    // second to read the actual label. Using a dummy buffer for the
    // first call avoids passing nullptr to getsockopt (undefined on
    // some implementations).
    char secBuf;
    socklen_t secLen = sizeof(secBuf);
    if (getsockopt(m_socket.socketDescriptor(), SOL_SOCKET, SO_PEERSEC, &secBuf, &secLen) == 0
        && secLen > 0) {
        // secLen is now the actual label length. SELinux labels are
        // null-terminated, so the returned length includes the NUL
        // byte — we skip it in fromUtf8.
        QVector<char> buf(secLen);
        if (getsockopt(m_socket.socketDescriptor(), SOL_SOCKET, SO_PEERSEC, buf.data(), &secLen)
            == 0) {
            info.peerSecurityContext = QString::fromUtf8(buf.data(), secLen - 1);
        }
    }
#endif
    return info;
}

bool NiriConnection::isConnected() const
{
    return m_connected;
}
QString NiriConnection::socketPath() const
{
    return m_socketPath;
}
QString NiriConnection::errorString() const
{
    return m_socket.errorString();
}

void NiriConnection::connectToSocket(const QString &path)
{
    if (m_socket.state() != QLocalSocket::UnconnectedState) {
        m_socket.abort();
    }
    m_reconnectTimer.stop();
    m_intentionalDisconnect = false;
    m_connected = false;
    m_subscriptionConfirmed = false;
    m_readBuffer.clear();

    m_socketPath = path.isEmpty() ? QString::fromUtf8(qgetenv("NIRI_SOCKET")) : path;
    qInfo() << "NiriConnection: connecting to"
            << (m_socketPath.isEmpty() ? "(empty!)" : m_socketPath);
    m_socket.connectToServer(m_socketPath);
}

void NiriConnection::disconnect()
{
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();
    m_reconnectAttempt = 0;
    m_socket.abort();
    m_connected = false;
    m_subscriptionConfirmed = false;
    m_readBuffer.clear();
}

void NiriConnection::setAutoReconnect(bool enabled)
{
    m_autoReconnect = enabled;
    if (!enabled)
        m_reconnectTimer.stop();
}

void NiriConnection::scheduleReconnect()
{
    if (!m_autoReconnect || m_intentionalDisconnect || m_socketPath.isEmpty())
        return;
    if (m_reconnectTimer.isActive())
        return;
    // Exponential backoff: 500ms, 1s, 2s, 4s, ... capped at 30s
    int delayMs = qMin(30000, 500 << qMin(m_reconnectAttempt, 6));
    m_reconnectAttempt++;
    qInfo() << "NiriConnection: reconnect scheduled in" << delayMs << "ms (attempt"
            << m_reconnectAttempt << ")";
    emit reconnectScheduled(delayMs, m_reconnectAttempt);
    m_reconnectTimer.start(delayMs);
}

void NiriConnection::onConnected()
{
    qInfo() << "NiriConnection: connected, sending subscription";
    m_reconnectAttempt = 0;
    m_connected = true;
    emit connectedChanged();
    sendEventStreamSubscription();
}

void NiriConnection::onDisconnected()
{
    bool wasConnected = m_connected;
    m_connected = false;
    m_subscriptionConfirmed = false;
    m_readBuffer.clear();
    emit connectedChanged();
    if (wasConnected)
        scheduleReconnect();
}

void NiriConnection::onErrorOccurred(QLocalSocket::LocalSocketError error)
{
    Q_UNUSED(error)
    qWarning() << "NiriConnection: socket error:" << m_socket.errorString()
               << "| path:" << m_socketPath;
    m_connected = false;
    m_subscriptionConfirmed = false;
    m_readBuffer.clear();
    emit connectedChanged();
    scheduleReconnect();
}

void NiriConnection::onReadyRead()
{
    m_readBuffer.feed(m_socket.readAll());
    for (const QByteArray &line : m_readBuffer.takeCompleteLines())
        processLine(line);
}

void NiriConnection::processLine(const QByteArray &line)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);

    if (err.error != QJsonParseError::NoError) {
        qWarning() << "Niri: failed to parse JSON:" << err.errorString() << "| line:" << line;
        return;
    }

    if (!doc.isObject()) {
        qWarning() << "Niri: expected JSON object, got:" << line;
        return;
    }

    QJsonObject obj = doc.object();

    if (!m_subscriptionConfirmed) {
        // First reply: {"Ok":"Handled"} or {"Err":"..."}
        if (obj.contains("Ok")) {
            qInfo() << "NiriConnection: subscription confirmed";
        } else if (obj.contains("Err")) {
            qWarning() << "Niri: subscription error:" << obj["Err"].toString();
        }
        m_subscriptionConfirmed = true;
    } else {
        // Subsequent: {"EventName":{"payload":...}}
        // Object has exactly one key: the event name
        for (auto it = obj.begin(); it != obj.end(); ++it)
            emit eventReceived(it.key(), it.value().toObject().toVariantMap());
    }
}

void NiriConnection::sendEventStreamSubscription()
{
    // Wire format: bare JSON string "EventStream"
    m_socket.write("\"EventStream\"\n");
    m_socket.flush();
}
