#pragma once

#include "nirisession.h"
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

class NiriConnection : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(NiriConnection)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString socketPath READ socketPath NOTIFY socketPathChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QVariantMap error READ error NOTIFY errorChanged)
    Q_PROPERTY(PeerInfo peerInfo READ peerInfo NOTIFY peerInfoChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(EndpointSource endpointSource READ endpointSource NOTIFY endpointSourceChanged)
    Q_PROPERTY(bool subscribed READ subscribed NOTIFY subscribedChanged)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY
                       autoReconnectChanged)

public:
    enum Status {
        Stopped = NiriSession::Stopped,
        Connecting = NiriSession::Connecting,
        Subscribing = NiriSession::Subscribing,
        Streaming = NiriSession::Streaming,
        Backoff = NiriSession::Backoff,
        Failed = NiriSession::Failed
    };
    Q_ENUM(Status)
    enum EndpointSource {
        Environment = NiriSession::Environment,
        Explicit = NiriSession::Explicit
    };
    Q_ENUM(EndpointSource)
    static NiriConnection *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriConnection *instance();
    bool isConnected() const;
    QString socketPath() const;
    QString errorString() const;
    QVariantMap error() const;
    PeerInfo peerInfo() const;
    Status status() const;
    EndpointSource endpointSource() const;
    bool subscribed() const;
    bool autoReconnect() const;

    // Applies to the next connection attempt.
    void setMaxFrameBytes(qsizetype bytes);
    qsizetype maxFrameBytes() const;

public slots:
    void connectToSocket(const QString &path = QString());
    void disconnect();
    void start();
    void stop();
    void selectEndpoint(const QString &path);
    void useEnvironmentEndpoint();
    void setAutoReconnect(bool enabled);

signals:
    void connectedChanged();
    void socketPathChanged();
    void endpointSourceChanged();
    void statusChanged();
    void subscribedChanged();
    void autoReconnectChanged();
    void errorChanged();
    void peerInfoChanged();
    void eventReceived(const QString &name, const QVariantMap &payload);
    void reconnectScheduled(int delayMs, int attempt);

private:
    explicit NiriConnection(QObject *parent);
    NiriSession *m_session;
};
