#pragma once

#include "niriwire.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QQueue>
#include <QTemporaryDir>

// Scripted released-protocol peer, not a second production reducer.
class NiriMock : public QObject
{
    Q_OBJECT
public:
    struct Fault
    {
        int delayMs = 0;
        bool drop = false;
        bool close = false;
        QByteArray bytes; // Exact replacement, including framing (malformed/oversize/Err).
        QList<qsizetype> fragments;
    };
    explicit NiriMock(QObject *parent = nullptr);
    ~NiriMock();
    bool start();
    void stop();
    QString socketPath() const;
    void setWindows(const QJsonArray &windows);
    void setWorkspaces(const QJsonArray &workspaces);
    void setKeyboardLayouts(const std::optional<NiriWire::WireValue> &layouts);
    void setNextActionError(const QString &message);
    void enqueueFault(const Fault &fault);
    void setResponse(const QString &request, const NiriWire::WireValue &response);
    void sendEvent(const QJsonObject &event);
    void sendEventBytes(const QByteArray &bytes);
    int streamCount() const;
signals:
    void connected();
    void disconnected();
    void requestReceived(const QJsonObject &request);
    void requestLine(quint64 clientId, const QByteArray &bytes);

private:
    struct Client : QObject
    {
        explicit Client(QObject *parent) : QObject(parent) { }
        QPointer<QLocalSocket> socket;
        QByteArray input;
        qsizetype scan = 0;
        quint64 id = 0;
        // Input stops immediately; broadcasts wait for the queued initial replication.
        bool stream = false;
        bool broadcastReady = false;
    };
    void accept();
    void read(Client *client);
    void request(Client *client, const QByteArray &line);
    void reply(Client *client, const QByteArray &bytes, const Fault &fault,
               bool activateStream = false);
    QByteArray initialBurst() const;
    static QByteArray line(const NiriWire::WireValue &value);
    static NiriWire::WireValue fixture(const QString &group, const QString &name);
    QLocalServer m_server;
    QTemporaryDir m_directory;
    QList<QPointer<Client>> m_clients;
    QMap<QString, NiriWire::WireValue> m_responses;
    QQueue<Fault> m_faults;
    NiriWire::WireValue m_windows{NiriWire::WireValue::Array{}};
    NiriWire::WireValue m_workspaces{NiriWire::WireValue::Array{}};
    std::optional<NiriWire::WireValue> m_layouts;
    QString m_nextActionError;
    quint64 m_nextClient = 0;
};
