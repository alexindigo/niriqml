#include "niriconnection.h"

#include <QDebug>

NiriConnection *NiriConnection::instance()
{
    static NiriConnection s;
    return &s;
}

NiriConnection::NiriConnection(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QLocalSocket::connected, this, &NiriConnection::onConnected);
    connect(&m_socket, &QLocalSocket::disconnected, this, &NiriConnection::onDisconnected);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, &NiriConnection::onErrorOccurred);
}

bool NiriConnection::isConnected() const { return m_connected; }

QString NiriConnection::socketPath() const { return m_socketPath; }

QString NiriConnection::errorString() const { return m_socket.errorString(); }

QLocalSocket *NiriConnection::socket() { return &m_socket; }

void NiriConnection::connectToSocket(const QString &path)
{
    if (m_connected)
        disconnect();

    m_socketPath = path.isEmpty() ? QString::fromUtf8(qgetenv("NIRI_SOCKET")) : path;
    m_socket.connectToServer(m_socketPath);
}

void NiriConnection::disconnect()
{
    m_socket.disconnectFromServer();
}

void NiriConnection::onConnected()
{
    m_connected = true;
    emit connectedChanged();
}

void NiriConnection::onDisconnected()
{
    m_connected = false;
    emit connectedChanged();
}

void NiriConnection::onErrorOccurred(QLocalSocket::LocalSocketError error)
{
    Q_UNUSED(error);
    m_connected = false;
    qDebug() << "NiriConnection error:" << m_socket.errorString();
    emit connectedChanged();
}
