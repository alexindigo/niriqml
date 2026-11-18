#include "nirirequests.h"
#include "nirirequestsequence.h"
#include "niritypes.h"
#include "generated/niriipccodec_generated.h"
#include <QCoreApplication>
#include <QJSValue>
#include <QJsonArray>
#include <cmath>
#include <limits>

using namespace NiriWire;

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

NiriRequests::NiriRequests(QObject *parent) : NiriIpcRequests(parent) { }

QVariant NiriRequests::number(const QString &text)
{
    const auto token = NumberToken::fromText(text.toLatin1());
    return token ? QVariant::fromValue(*token) : QVariant();
}

NiriRequestOperation::Options NiriRequests::options() const
{
    NiriRequestOperation::Options options;
    options.timeoutMs = m_requestTimeoutMs;
    options.maxFrameBytes = m_maxFrameBytes;
    return options;
}

bool NiriRequests::parseOptions(const QVariantMap &input, NiriRequestOperation::Options &options,
                                QString &error)
{
    for (auto it = input.begin(); it != input.end(); ++it) {
        if (it.key() != "timeoutMs" && it.key() != "connectTimeoutMs") {
            error = QStringLiteral("Unknown request option: %1").arg(it.key());
            return false;
        }
        bool valid = false;
        const auto value = it.value().toDouble(&valid);
        const auto minimum = it.key() == "timeoutMs" ? -1 : 1;
        if (!valid || !std::isfinite(value) || std::floor(value) != value || value < minimum
            || value > std::numeric_limits<int>::max()) {
            error = QStringLiteral("Invalid request option: %1").arg(it.key());
            return false;
        }
        if (it.key() == "timeoutMs")
            options.timeoutMs = int(value);
        else
            options.connectTimeoutMs = int(value);
    }
    return true;
}

std::optional<WireValue> NiriRequests::wireValue(const QVariant &value, QString &error, int depth)
{
    if (depth > 128) {
        error = QStringLiteral("Request nesting exceeds 128");
        return {};
    }
    if (value.metaType() == QMetaType::fromType<QJSValue>())
        return wireValue(value.value<QJSValue>().toVariant(), error, depth + 1);
    if (value.metaType() == QMetaType::fromType<NumberToken>())
        return WireValue(value.value<NumberToken>());
    switch (value.typeId()) {
    case QMetaType::Nullptr:
        return WireValue();
    case QMetaType::Bool:
        return WireValue(value.toBool());
    case QMetaType::QString:
        return WireValue(value.toString());
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        return WireValue(NumberToken::fromUInt64(value.toULongLong()));
    case QMetaType::Int:
    case QMetaType::LongLong:
        return WireValue(NumberToken::fromInt64(value.toLongLong()));
    case QMetaType::Double:
    case QMetaType::Float:
        if (std::isfinite(value.toDouble())) {
            Error conversionError;
            const auto wire = fromQJsonValueLossy(QJsonValue(value.toDouble()), &conversionError);
            if (!wire)
                error = conversionError.reason;
            return wire;
        }
        break;
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        WireValue::Array items;
        for (const auto &item : value.toList()) {
            auto wire = wireValue(item, error, depth + 1);
            if (!wire)
                return {};
            items.append(*wire);
        }
        return WireValue(items);
    }
    case QMetaType::QVariantMap: {
        WireValue::Object items;
        const auto map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) {
            auto wire = wireValue(it.value(), error, depth + 1);
            if (!wire)
                return {};
            items.append({it.key(), *wire});
        }
        Error conversionError;
        const auto wire = WireValue::object(items, &conversionError);
        if (!wire)
            error = conversionError.reason;
        return wire;
    }
    default:
        break;
    }
    error = QStringLiteral("Value is not a supported JSON value");
    return {};
}

NiriPendingReply *NiriRequests::dispatch(const WireValue &request, QObject *scope,
                                         NiriRequestOperation::Options options, QObject *owner,
                                         NiriRequestOperation::Decoder decoder,
                                         const QString &validationError,
                                         NiriRequestOperation::Channel channel, bool persistent,
                                         bool start, NiriRequestOperation **operationOut,
                                         std::function<NiriPendingReply *(QObject *)> replyFactory)
{
    if (!NiriSession::checkThread(scope) || (owner && !NiriSession::checkThread(owner)))
        return nullptr;
    auto *session = NiriSession::instance();
    session->ensureStarted();
    QPointer<QObject> scopeGuard(scope), ownerGuard(owner);
    // Resolving Environment can notify consumers. Capture before constructing
    // reply/operation resources, and never reuse an owner deleted by a handler.
    const auto endpoint = session->currentEndpoint();
    if (owner && !ownerGuard)
        return nullptr;
    auto *reply = replyFactory ? replyFactory(ownerGuard) : new NiriPendingReply(ownerGuard);
    QJSEngine::setObjectOwnership(reply,
                                  owner ? QJSEngine::CppOwnership : QJSEngine::JavaScriptOwnership);
    auto *operation = new NiriRequestOperation(reply, scopeGuard, endpoint, request, options,
                                               std::move(decoder), std::move(channel), persistent,
                                               validationError);
    if (operationOut)
        *operationOut = operation;
    if (start)
        operation->start();
    return reply;
}

NiriPendingReply *NiriRequests::sendRaw(const QVariant &request, const QVariantMap &input)
{
    if (!NiriSession::checkThread(this))
        return nullptr;
    QString error;
    auto opts = options();
    auto wire = wireValue(request, error);
    if (error.isEmpty())
        parseOptions(input, opts, error);
    return dispatch(wire.value_or(WireValue()), this, opts, nullptr, {}, error);
}

NiriPendingReply *NiriRequests::sendRaw(const QJsonObject &request)
{
    return sendRaw(request.toVariantMap());
}

NiriPendingReply *NiriRequests::sendRawJson(const QString &json, const QVariantMap &input)
{
    if (!NiriSession::checkThread(this))
        return nullptr;
    Error parseError;
    const auto document = parse(json.toUtf8(), &parseError);
    QString error = document ? QString() : parseError.reason;
    auto opts = options();
    if (error.isEmpty())
        parseOptions(input, opts, error);
    return dispatch(document ? document->root : WireValue(), this, opts, nullptr, {}, error);
}

NiriPendingReply *NiriRequests::sendWire(const WireValue &request,
                                         NiriRequestOperation::Options options, QObject *owner)
{
    return dispatch(request, this, options, owner);
}

void NiriRequests::send(const QJsonObject &request, Callback callback)
{
    sendJson(request, std::move(callback));
}

void NiriRequests::sendJson(const QJsonValue &request, Callback callback)
{
    if (!NiriSession::checkThread(this))
        return;
    Error error;
    const auto wire = fromQJsonValueLossy(request, &error);
    auto *reply = dispatch(wire.value_or(WireValue()), this, options(), this, {}, error.reason);
    if (!reply)
        return;
    connect(reply, &NiriPendingReply::finished, this,
            [reply = QPointer<NiriPendingReply>(reply), callback = std::move(callback)] {
                if (!reply)
                    return;
                const auto ok = !reply->isError();
                const auto value = ok
                        ? toQJsonValueLossy(reply->wireDocument().root.lookup("Ok")
                                                    ? *reply->wireDocument().root.lookup("Ok")
                                                    : WireValue())
                        : std::optional<QJsonValue>();
                const auto result = ok ? value.value_or(QJsonValue()).toObject()
                                       : QJsonObject{{"error", reply->error().message}};
                reply->deleteLater();
                callback(ok, result);
            });
}

NiriPendingReply *NiriRequests::dispatchTyped(const WireValue &request, QObject *scope,
                                              NiriRequestOperation::Options opts,
                                              const QString &expected,
                                              const QString &validationError)
{
    return dispatch(
            request, scope, opts, nullptr,
            [expected](const WireValue &ok, QVariant &value, NiriError &error) {
                return NiriIpc::decodeResponse(expected, ok, value, error);
            },
            validationError, {}, false, true, nullptr,
            [expected](QObject *owner) { return NiriIpc::createReply(expected, owner); });
}

NiriPendingReply *NiriRequests::submitRequest(const WireValue &request, const QString &expected,
                                              const QVariantMap &input, bool interactive,
                                              bool eventStream, const QString &validationError)
{
    if (!NiriSession::checkThread(this))
        return nullptr;
    auto opts = options();
    if (interactive)
        opts.timeoutMs = -1;
    opts.observeEventStream = eventStream;
    QString error = validationError;
    if (error.isEmpty())
        parseOptions(input, opts, error);
    return dispatchTyped(request, this, opts, expected, error);
}

NiriRequestSequence *NiriRequests::sequence()
{
    if (!NiriSession::checkThread(this))
        return nullptr;
    const auto opts = options();
    QPointer<QObject> scope(this);
    const auto endpoint = NiriSession::instance()->currentEndpoint();
    auto *sequence = new NiriRequestSequence(scope, opts, endpoint.revision);
    QJSEngine::setObjectOwnership(sequence, QJSEngine::JavaScriptOwnership);
    return sequence;
}
