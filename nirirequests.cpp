#include "nirirequests.h"

#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>
#include <QCoreApplication>

#include "niriconnection.h"
#include "nirierror.h"
#include "niritypes.h"
#include "niriutils.h"

NiriRequests *NiriRequests::create(QQmlEngine *engine, QJSEngine *jsEngine)
{
    if (!NiriSession::checkThread(engine ? engine : jsEngine))
        return nullptr;
    QObject *owner = engine ? static_cast<QObject *>(engine) : static_cast<QObject *>(jsEngine);
    if (!owner)
        return instance();
    auto *self = new NiriRequests(owner);
    QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    NiriSession::instance()->ensureStarted();
    return self;
}

NiriRequests *NiriRequests::instance()
{
    if (!NiriSession::checkThread())
        return nullptr;
    auto *app = QCoreApplication::instance();
    auto *self = app->findChild<NiriRequests *>(QStringLiteral("niriqml.requests"),
                                                Qt::FindDirectChildrenOnly);
    if (!self) {
        self = new NiriRequests(app);
        self->setObjectName(QStringLiteral("niriqml.requests"));
    }
    NiriSession::instance()->ensureStarted();
    return self;
}

NiriRequests::NiriRequests(QObject *parent) : QObject(parent) { }

NiriRequests::PendingRequest::~PendingRequest()
{
    // Requests are facade children, including when the facade is torn down
    // without completion. Retire I/O now, but release Qt senders only after
    // their signal stacks unwind (a finished handler may destroy the engine).
    if (timeout) {
        timeout->stop();
        timeout->disconnect();
        timeout->setParent(nullptr);
        timeout->deleteLater();
    }
    if (socket) {
        socket->disconnect();
        socket->setParent(nullptr);
        socket->abort();
        socket->deleteLater();
    }
}

QString NiriRequests::socketPath() const
{
    if (!NiriSession::checkThread(this))
        return {};
    return NiriSession::instance()->currentEndpoint().path;
}

void NiriRequests::completeRequest(PendingRequest *pr, bool ok, const QJsonObject &result)
{
    if (pr->finished)
        return;
    pr->finished = true;

    Callback cb = std::move(pr->callback);
    delete pr;

    cb(ok, result);
}

void NiriRequests::send(const QJsonObject &request, Callback callback)
{
    sendJson(QJsonValue(request), callback);
}

void NiriRequests::sendJson(const QJsonValue &request, Callback callback)
{
    if (!NiriSession::checkThread(this))
        return;
    NiriSession::instance()->ensureStarted();
    auto *pr = new PendingRequest(this);
    pr->recvBuffer = NiriLineBuffer(m_maxFrameBytes);
    pr->socket = new QLocalSocket(pr);
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

    auto *timeout = new QTimer(pr);
    timeout->setSingleShot(true);
    // Context = the timer itself: when completeRequest disarms and deletes
    // the timer, this connection is severed, so the lambda can never fire
    // after pr has been deleted (previously the context was `this`, so a
    // late/spurious timeout dereferenced freed PendingRequest — UAF).
    connect(timeout, &QTimer::timeout, timeout,
            [pr, this]() { completeRequest(pr, false, QJsonObject{{"error", "Timeout"}}); });
    timeout->start(m_requestTimeoutMs);
    pr->timeout = timeout;

    connect(pr->socket, &QLocalSocket::connected, this,
            [pr]() { pr->socket->write(pr->sendBuffer); });

    connect(pr->socket, &QLocalSocket::readyRead, this, [this, pr]() {
        pr->recvBuffer.feed(pr->socket->readAll());
        const auto lines = pr->recvBuffer.takeCompleteLines();
        if (lines.isEmpty()) {
            if (pr->recvBuffer.error().code != NiriLineBuffer::ErrorCode::None) {
                const auto error = QStringLiteral("Framing error: frame exceeds %1 bytes")
                                           .arg(pr->recvBuffer.error().allowedBytes);
                completeRequest(pr, false, QJsonObject{{"error", error}});
            }
            return;
        }

        // A one-shot operation ends at its first complete reply. A framing
        // failure in a later line cannot invalidate that already-received reply,
        // regardless of how the socket groups those bytes into readyRead calls.
        const QByteArray &line = lines.first();

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

    connect(pr->socket, &QLocalSocket::errorOccurred, this,
            [this, pr](QLocalSocket::LocalSocketError) {
                completeRequest(pr, false,
                                QJsonObject{{"error",
                                             pr->socket ? pr->socket->errorString()
                                                        : QStringLiteral("socket error")}});
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
    if (!NiriSession::checkThread(this))
        return nullptr;
    auto *reply = new NiriPendingReply(this);
    sendJson(QJsonValue(query),
             [reply](bool ok, const QJsonObject &result) { wireRawReply(reply, ok, result); });
    return reply;
}

NiriPendingReply *NiriRequests::sendRaw(const QJsonObject &request)
{
    if (!NiriSession::checkThread(this))
        return nullptr;
    auto *reply = new NiriPendingReply(this);
    sendJson(QJsonValue(request),
             [reply](bool ok, const QJsonObject &result) { wireRawReply(reply, ok, result); });
    return reply;
}

// ── Typed query wrappers ──

NiriPendingReply *NiriRequests::windows()
{
    if (!NiriSession::checkThread(this))
        return nullptr;
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
    if (!NiriSession::checkThread(this))
        return nullptr;
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
    if (!NiriSession::checkThread(this))
        return nullptr;
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
    if (!NiriSession::checkThread(this))
        return nullptr;
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
    if (!NiriSession::checkThread(this))
        return nullptr;
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
    if (!NiriSession::checkThread(this))
        return nullptr;
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
    if (!NiriSession::checkThread(this))
        return nullptr;
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
