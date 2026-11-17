#include "nirirequests.h"
#include "nirirequestsequence.h"
#include "niritypes.h"
#include "niriutils.h"
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

NiriRequests::NiriRequests(QObject *parent) : QObject(parent) { }

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
        if (std::isfinite(value.toDouble()))
            return fromQJsonValueLossy(QJsonValue(value.toDouble()));
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
        return WireValue::object(items);
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
                                         bool start, NiriRequestOperation **operationOut)
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
    auto *reply = new NiriPendingReply(ownerGuard);
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

// Transitional named decoders. E6 replaces these compatibility gadget
// conversions with generated field/range validation; the response discriminator
// and outer payload kind are checked here before any conversion can default.
NiriPendingReply *NiriRequests::query(const QString &kind)
{
    if (!NiriSession::checkThread(this))
        return nullptr;
    auto decode = [kind](const WireValue &ok, QVariant &value, NiriError &error) {
        const auto *payload = ok.lookup(kind);
        const bool list = kind == "Windows" || kind == "Workspaces";
        const bool focused = kind == "FocusedWindow" || kind == "FocusedOutput";
        bool valid = ok.objectValue() && ok.objectValue()->size() == 1 && payload;
        if (valid) {
            if (kind == "Version")
                valid = payload->stringValue();
            else if (list)
                valid = payload->arrayValue();
            else
                valid = payload->objectValue()
                        || (focused && payload->type() == WireValue::Type::Null);
        }
        if (!valid) {
            error.message = QStringLiteral("Expected %1 response and payload type").arg(kind);
            error.fieldPath = QStringLiteral("$.Ok.%1").arg(kind);
            return false;
        }
        if (kind == "Version") {
            value = *payload->stringValue();
            return true;
        }
        if (payload->type() == WireValue::Type::Null) {
            value = QVariant::fromValue(nullptr);
            return true;
        }
        auto gadget = [&error, &kind](const WireValue &wire, QMetaType type) {
            const auto json = toQJsonValueLossy(wire);
            if (!json) {
                error.message = QStringLiteral("Response cannot be projected to a legacy gadget");
                error.fieldPath = QStringLiteral("$.Ok.%1").arg(kind);
                return QVariant();
            }
            return jsonToGadget(json->toObject(), type);
        };
        const auto type = (kind == "Windows" || kind == "FocusedWindow")
                ? QMetaType::fromType<NiriWindow>()
                : kind == "Workspaces"      ? QMetaType::fromType<NiriWorkspace>()
                : kind == "KeyboardLayouts" ? QMetaType::fromType<NiriKeyboardLayouts>()
                                            : QMetaType::fromType<NiriOutput>();
        if (list) {
            QVariantList items;
            for (const auto &item : *payload->arrayValue()) {
                if (!item.objectValue()) {
                    error.message = QStringLiteral("Expected record in response array");
                    error.fieldPath = QStringLiteral("$.Ok.%1[%2]").arg(kind).arg(items.size());
                    return false;
                }
                items.append(gadget(item, type));
            }
            value = items;
        } else if (kind == "Outputs") {
            QVariantMap items;
            for (const auto &item : *payload->objectValue()) {
                if (!item.second.objectValue()) {
                    error.message = QStringLiteral("Expected output record");
                    error.fieldPath = QStringLiteral("$.Ok.Outputs.%1").arg(item.first);
                    return false;
                }
                items.insert(item.first, gadget(item.second, type));
            }
            value = items;
        } else {
            value = gadget(*payload, type);
        }
        return error.message.isEmpty();
    };
    return dispatch(WireValue(kind), this, options(), nullptr, std::move(decode));
}

NiriPendingReply *NiriRequests::windows()
{
    return query(QStringLiteral("Windows"));
}
NiriPendingReply *NiriRequests::workspaces()
{
    return query(QStringLiteral("Workspaces"));
}
NiriPendingReply *NiriRequests::outputs()
{
    return query(QStringLiteral("Outputs"));
}
NiriPendingReply *NiriRequests::focusedWindow()
{
    return query(QStringLiteral("FocusedWindow"));
}
NiriPendingReply *NiriRequests::focusedOutput()
{
    return query(QStringLiteral("FocusedOutput"));
}
NiriPendingReply *NiriRequests::keyboardLayouts()
{
    return query(QStringLiteral("KeyboardLayouts"));
}
NiriPendingReply *NiriRequests::version()
{
    return query(QStringLiteral("Version"));
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
