#pragma once

#include <QObject>
#include <QLocalSocket>
#include <QByteArray>
#include <QTimer>
#include <QVariantMap>
#include <QJsonObject>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

#include "nirilinebuffer.h"

class NiriConnection : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(NiriConnection)

    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString socketPath READ socketPath NOTIFY connectedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY connectedChanged)

public:
    static NiriConnection *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriConnection *instance();

    bool isConnected() const;
    QString socketPath() const;
    QString errorString() const;

public slots:
    void connectToSocket(const QString &path = QString());
    void disconnect();
    void setAutoReconnect(bool enabled);

signals:
    void connectedChanged();
    void eventReceived(const QString &name, const QVariantMap &payload);
    void reconnectScheduled(int delayMs, int attempt);

private:
    explicit NiriConnection(QObject *parent = nullptr);

    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QLocalSocket::LocalSocketError error);
    void onReadyRead();
    void processLine(const QByteArray &line);
    void sendEventStreamSubscription();

    void scheduleReconnect();

    QLocalSocket m_socket;
    NiriLineBuffer m_readBuffer;
    QString m_socketPath;
    bool m_connected = false;
    bool m_subscriptionConfirmed = false;
    bool m_autoReconnect = true;
    bool m_intentionalDisconnect = false;
    int m_reconnectAttempt = 0;
    QTimer m_reconnectTimer;
};
