#pragma once

#include "nirilinebuffer.h"
#include "niritypes.h"
#include "niriwire.h"

#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <memory>

// Application-owned runtime. All acquisition and use is on the application
// thread. Public QML objects are engine-owned facades, never this runtime.
class NiriSession : public QObject
{
    Q_OBJECT
public:
    enum Status { Stopped, Connecting, Subscribing, Streaming, Backoff, Failed };
    Q_ENUM(Status)
    enum EndpointSource { Environment, Explicit };
    Q_ENUM(EndpointSource)
    struct Endpoint
    {
        QString path;
        quint64 revision = 0;
        EndpointSource source = Environment;
    };

    static bool checkThread(const QObject *consumer = nullptr);
    static NiriSession *instance();
    ~NiriSession() override;

    Endpoint currentEndpoint();
    quint64 epoch() const;
    Status status() const;
    bool isConnected() const;
    bool subscribed() const;
    bool autoReconnect() const;
    QVariantMap error() const;
    QString errorString() const;
    PeerInfo peerInfo() const;

    void ensureStarted();
    void connectToSocket(const QString &path = {});
    void start();
    void stop();
    void selectEndpoint(const QString &path);
    void useEnvironmentEndpoint();
    void setAutoReconnect(bool enabled);
    void setConnectTimeoutMs(int timeoutMs);
    void setHandshakeTimeoutMs(int timeoutMs);
    void setMaxFrameBytes(qsizetype bytes);
    qsizetype maxFrameBytes() const;
    // Reducer rejection uses the same reset/failure/reconnect transition.
    void protocolError(quint64 epoch, const QString &message, const QByteArray &raw = {});

signals:
    void socketPathChanged();
    void endpointSourceChanged();
    void endpointChanged(); // includes revision-only changes, for E5 cancellation
    void statusChanged();
    void connectedChanged();
    void subscribedChanged();
    void autoReconnectChanged();
    void errorChanged();
    void peerInfoChanged();
    void reconnectScheduled(int delayMs, int attempt);
    // E7 integration points: reset/apply canonical state here, before public
    // event publication. Each carries the epoch; consumers must reject stale work.
    void streamReset(quint64 epoch, const QString &reason);
    void subscriptionAccepted(quint64 epoch);
    void eventFrame(const NiriWire::Document &document, quint64 epoch);
    void eventReceived(const QString &name, const QVariantMap &payload);

private:
    explicit NiriSession(QObject *parent);
    struct Attempt;
    struct Snapshot;
    Snapshot snapshot() const;
    bool publish(quint64 token);
    bool current(const std::shared_ptr<Attempt> &attempt) const;
    void select(const QString &path, EndpointSource source);
    void queueAttempt();
    void beginAttempt(quint64 token);
    void retireAttempt();
    void connected(const std::shared_ptr<Attempt> &attempt);
    void drain(const std::shared_ptr<Attempt> &attempt);
    void processFrame(const std::shared_ptr<Attempt> &attempt, const QByteArray &frame);
    void applyEvent(const NiriWire::Document &document, quint64 epoch);
    void fail(const QString &category, const QString &code, const QString &message,
              const QByteArray &raw = {});
    void commitRetry();
    void notifyRetry(quint64 token);

    Endpoint m_endpoint;
    bool m_endpointResolved = false;
    bool m_everStarted = false;
    bool m_wantedRunning = false;
    bool m_shuttingDown = false;
    bool m_autoReconnect = true;
    bool m_attemptQueued = false;
    quint64 m_epoch = 0;
    Status m_status = Stopped;
    QVariantMap m_error;
    PeerInfo m_peerInfo;
    std::shared_ptr<Attempt> m_attempt;
    std::unique_ptr<Snapshot> m_announced;
    bool m_publishing = false;
    QTimer *m_retry;
    int m_reconnectAttempt = 0;
    quint64 m_retrySerial = 0;
    quint64 m_announcedRetrySerial = 0;
    int m_connectTimeoutMs = 5000;
    int m_handshakeTimeoutMs = 5000;
    qsizetype m_maxFrameBytes = NiriLineBuffer::DefaultMaxFrameBytes;
};
