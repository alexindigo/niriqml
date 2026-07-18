#include "nirirequests.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>

#include "niriconnection.h"

NiriRequests *NiriRequests::instance()
{
    static NiriRequests s;
    return &s;
}

NiriRequests::NiriRequests(QObject *parent)
    : QObject(parent)
{
}

QString NiriRequests::socketPath() const
{
    // Prefer NiriConnection's active path (so tests using a mock socket also work)
    if (NiriConnection::instance()->isConnected())
        return NiriConnection::instance()->socketPath();
    return QString::fromUtf8(qgetenv("NIRI_SOCKET"));
}

void NiriRequests::completeRequest(PendingRequest *pr, bool ok, const QJsonObject &result)
{
    if (pr->finished)
        return;
    pr->finished = true;

    Callback cb = pr->callback;

    if (pr->socket) {
        pr->socket->disconnect();
        pr->socket->deleteLater();
        pr->socket = nullptr;
    }
    delete pr;

    cb(ok, result);
}

void NiriRequests::send(const QJsonObject &request, Callback callback)
{
    sendJson(QJsonValue(request), callback);
}

void NiriRequests::sendJson(const QJsonValue &request, Callback callback)
{
    auto *pr = new PendingRequest;
    pr->socket = new QLocalSocket(this);
    pr->callback = callback;

    // QJsonDocument only supports arrays/objects as roots; for bare strings we
    // serialize manually (Qt's JSON parser accepts them on decode).
    if (request.isString()) {
        pr->sendBuffer = QByteArray("\"") + request.toString().toUtf8() + "\"\n";
    } else if (request.isArray()) {
        pr->sendBuffer = QJsonDocument(request.toArray()).toJson(QJsonDocument::Compact) + "\n";
    } else if (request.isObject()) {
        pr->sendBuffer = QJsonDocument(request.toObject()).toJson(QJsonDocument::Compact) + "\n";
    } else {
        pr->sendBuffer = QJsonDocument::fromVariant(request.toVariant()).toJson(QJsonDocument::Compact) + "\n";
    }

    connect(pr->socket, &QLocalSocket::connected, this, [pr]() {
        pr->socket->write(pr->sendBuffer);
    });

    connect(pr->socket, &QLocalSocket::readyRead, this, [this, pr]() {
        pr->recvBuffer += pr->socket->readAll();
        int idx = pr->recvBuffer.indexOf('\n');
        if (idx < 0)
            return;

        QByteArray line = pr->recvBuffer.left(idx).trimmed();

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError) {
            completeRequest(pr, false, QJsonObject{{"error", "Failed to parse reply"}});
            return;
        }

        QJsonObject obj = doc.object();
        if (obj.contains("Ok"))
            completeRequest(pr, true, obj["Ok"].toObject());
        else if (obj.contains("Err"))
            completeRequest(pr, false, QJsonObject{{"error", obj["Err"].toString()}});
        else
            completeRequest(pr, false, QJsonObject{{"error", "Unexpected reply format"}});
    });

    connect(pr->socket, &QLocalSocket::errorOccurred, this, [this, pr](QLocalSocket::LocalSocketError) {
        completeRequest(pr, false, QJsonObject{{"error", pr->socket ? pr->socket->errorString() : QStringLiteral("socket error")}});
    });

    pr->socket->connectToServer(socketPath());
}
