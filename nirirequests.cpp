#include "nirirequests.h"

#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>

#include "niriconnection.h"
#include "nirierror.h"
#include "niritypes.h"
#include "niriutils.h"

NiriRequests *NiriRequests::create(QQmlEngine *, QJSEngine *jsEngine)
{
    NiriRequests *self = instance();
    if (jsEngine)
        QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    return self;
}

NiriRequests *NiriRequests::instance()
{
    static NiriRequests s;
    return &s;
}

NiriRequests::NiriRequests(QObject *parent) : QObject(parent) { }

QString NiriRequests::socketPath() const
{
    // Use NiriConnection's path if connected (works both with mock and real niri)
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

    // QJsonDocument only supports arrays/objects as roots; for bare strings
    // we serialize manually (Qt's JSON parser accepts them on decode).
    if (request.isString()) {
        pr->sendBuffer = QByteArray("\"") + request.toString().toUtf8() + "\"\n";
    } else if (request.isArray()) {
        pr->sendBuffer = QJsonDocument(request.toArray()).toJson(QJsonDocument::Compact) + "\n";
    } else if (request.isObject()) {
        pr->sendBuffer = QJsonDocument(request.toObject()).toJson(QJsonDocument::Compact) + "\n";
    } else {
        // null/bool/number — rare
        pr->sendBuffer =
                QJsonDocument::fromVariant(request.toVariant()).toJson(QJsonDocument::Compact)
                + "\n";
    }

    connect(pr->socket, &QLocalSocket::connected, this,
            [pr]() { pr->socket->write(pr->sendBuffer); });

    connect(pr->socket, &QLocalSocket::readyRead, this, [this, pr]() {
        pr->recvBuffer += pr->socket->readAll();
        int idx = pr->recvBuffer.indexOf('\n');
        if (idx < 0)
            return;

        QByteArray line = pr->recvBuffer.left(idx).trimmed();

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError) {
            completeRequest(pr, false, QJsonObject{ { "error", "Failed to parse reply" } });
            return;
        }

        QJsonObject obj = doc.object();
        if (obj.contains("Ok"))
            completeRequest(pr, true, obj["Ok"].toObject());
        else if (obj.contains("Err"))
            completeRequest(pr, false, QJsonObject{ { "error", obj["Err"].toString() } });
        else
            completeRequest(pr, false, QJsonObject{ { "error", "Unexpected reply format" } });
    });

    connect(pr->socket, &QLocalSocket::errorOccurred, this,
            [this, pr](QLocalSocket::LocalSocketError) {
                completeRequest(pr, false,
                                QJsonObject{ { "error",
                                               pr->socket ? pr->socket->errorString()
                                                          : QStringLiteral("socket error") } });
            });

    pr->socket->connectToServer(socketPath());
}

// ── Generic raw send (escape hatch) ──

static void wireRawReply(NiriPendingReply *reply, bool ok, const QJsonObject &result)
{
    if (!ok) {
        NiriError err;
        err.code = -1;
        err.message = result.value("error").toString();
        reply->setError(err);
    } else {
        reply->setFinished(QVariant::fromValue(result.toVariantMap()));
    }
}

NiriPendingReply *NiriRequests::sendRaw(const QString &query)
{
    auto *reply = new NiriPendingReply(this);
    sendJson(QJsonValue(query),
             [reply](bool ok, const QJsonObject &result) { wireRawReply(reply, ok, result); });
    return reply;
}

NiriPendingReply *NiriRequests::sendRaw(const QJsonObject &request)
{
    auto *reply = new NiriPendingReply(this);
    sendJson(QJsonValue(request),
             [reply](bool ok, const QJsonObject &result) { wireRawReply(reply, ok, result); });
    return reply;
}

// ── Typed query wrappers ──

NiriPendingReply *NiriRequests::windows()
{
    auto *reply = new NiriPendingReply(this);
    sendJson(QJsonValue(QStringLiteral("Windows")), [reply](bool ok, const QJsonObject &result) {
        if (!ok) {
            NiriError err;
            err.code = -1;
            err.message = result.value("error").toString();
            reply->setError(err);
            return;
        }
        QJsonArray arr = result.value("Windows").toArray();
        QVariantList windows;
        windows.reserve(arr.size());
        for (const QJsonValue &v : arr)
            windows.append(jsonToGadget(v.toObject(), QMetaType::fromType<NiriWindow>()));
        reply->setFinished(windows);
    });
    return reply;
}

NiriPendingReply *NiriRequests::workspaces()
{
    auto *reply = new NiriPendingReply(this);
    sendJson(QJsonValue(QStringLiteral("Workspaces")), [reply](bool ok, const QJsonObject &result) {
        if (!ok) {
            NiriError err;
            err.code = -1;
            err.message = result.value("error").toString();
            reply->setError(err);
            return;
        }
        QJsonArray arr = result.value("Workspaces").toArray();
        QVariantList workspaces;
        workspaces.reserve(arr.size());
        for (const QJsonValue &v : arr)
            workspaces.append(jsonToGadget(v.toObject(), QMetaType::fromType<NiriWorkspace>()));
        reply->setFinished(workspaces);
    });
    return reply;
}

NiriPendingReply *NiriRequests::outputs()
{
    auto *reply = new NiriPendingReply(this);
    sendJson(QJsonValue(QStringLiteral("Outputs")), [reply](bool ok, const QJsonObject &result) {
        if (!ok) {
            NiriError err;
            err.code = -1;
            err.message = result.value("error").toString();
            reply->setError(err);
            return;
        }
        // Outputs is a map: {"eDP-1": {...}, "DP-2": {...}}
        QJsonObject obj = result.value("Outputs").toObject();
        QVariantMap outputs;
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            outputs.insert(it.key(),
                           jsonToGadget(it.value().toObject(), QMetaType::fromType<NiriOutput>()));
        }
        reply->setFinished(outputs);
    });
    return reply;
}

NiriPendingReply *NiriRequests::focusedWindow()
{
    auto *reply = new NiriPendingReply(this);
    sendJson(QJsonValue(QStringLiteral("FocusedWindow")),
             [reply](bool ok, const QJsonObject &result) {
                 if (!ok) {
                     NiriError err;
                     err.code = -1;
                     err.message = result.value("error").toString();
                     reply->setError(err);
                     return;
                 }
                 QJsonValue val = result.value("FocusedWindow");
                 if (val.isNull() || !val.isObject()) {
                     reply->setFinished(QVariant());
                     return;
                 }
                 reply->setFinished(
                         jsonToGadget(val.toObject(), QMetaType::fromType<NiriWindow>()));
             });
    return reply;
}

NiriPendingReply *NiriRequests::focusedOutput()
{
    auto *reply = new NiriPendingReply(this);
    sendJson(QJsonValue(QStringLiteral("FocusedOutput")),
             [reply](bool ok, const QJsonObject &result) {
                 if (!ok) {
                     NiriError err;
                     err.code = -1;
                     err.message = result.value("error").toString();
                     reply->setError(err);
                     return;
                 }
                 QJsonValue val = result.value("FocusedOutput");
                 if (val.isNull() || !val.isObject()) {
                     reply->setFinished(QVariant());
                     return;
                 }
                 reply->setFinished(
                         jsonToGadget(val.toObject(), QMetaType::fromType<NiriOutput>()));
             });
    return reply;
}

NiriPendingReply *NiriRequests::keyboardLayouts()
{
    auto *reply = new NiriPendingReply(this);
    sendJson(QJsonValue(QStringLiteral("KeyboardLayouts")),
             [reply](bool ok, const QJsonObject &result) {
                 if (!ok) {
                     NiriError err;
                     err.code = -1;
                     err.message = result.value("error").toString();
                     reply->setError(err);
                     return;
                 }
                 QJsonObject obj = result.value("KeyboardLayouts").toObject();
                 reply->setFinished(jsonToGadget(obj, QMetaType::fromType<NiriKeyboardLayouts>()));
             });
    return reply;
}

NiriPendingReply *NiriRequests::version()
{
    auto *reply = new NiriPendingReply(this);
    sendJson(QJsonValue(QStringLiteral("Version")), [reply](bool ok, const QJsonObject &result) {
        if (!ok) {
            NiriError err;
            err.code = -1;
            err.message = result.value("error").toString();
            reply->setError(err);
            return;
        }
        reply->setFinished(result.value("Version").toString());
    });
    return reply;
}
