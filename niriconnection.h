#pragma once

#include <QObject>
#include <QLocalSocket>
#include <QString>

class NiriConnection : public QObject
{
    Q_OBJECT

public:
    static NiriConnection *instance();

    bool isConnected() const;
    QString socketPath() const;
    QString errorString() const;

    QLocalSocket *socket();

public slots:
    void connectToSocket(const QString &path = QString());
    void disconnect();

signals:
    void connectedChanged();

private:
    explicit NiriConnection(QObject *parent = nullptr);

    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QLocalSocket::LocalSocketError error);

    QLocalSocket m_socket;
    QString m_socketPath;
    bool m_connected = false;
};
