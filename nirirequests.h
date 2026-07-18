#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QObject>
#include <functional>

class NiriRequests : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(bool ok, const QJsonObject &result)>;

    static NiriRequests *instance();

    // Wire primitives: send one JSON request over a fresh per-request socket,
    // receive the single-line reply, invoke callback with the parsed result.
    void send(const QJsonObject &request, Callback callback);
    void sendJson(const QJsonValue &request, Callback callback);

private:
    explicit NiriRequests(QObject *parent = nullptr);

    struct PendingRequest {
        QLocalSocket *socket = nullptr;
        QByteArray sendBuffer;
        QByteArray recvBuffer;
        Callback callback;
        bool finished = false;
    };

    QString socketPath() const;
    void completeRequest(PendingRequest *pr, bool ok, const QJsonObject &result);
};
