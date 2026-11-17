#include "niriactions.h"

#include <QJSEngine>
#include <QJsonObject>
#include <QCoreApplication>
#include "nirisession.h"

#include "nirierror.h"
#include "nirirequests.h"
#include "generated/niriipccodec_generated.h"

NiriActions *NiriActions::create(QQmlEngine *engine, QJSEngine *jsEngine)
{
    if (!NiriSession::checkThread(engine ? engine : jsEngine))
        return nullptr;
    QObject *owner = engine ? static_cast<QObject *>(engine) : static_cast<QObject *>(jsEngine);
    if (!owner)
        return instance();
    auto *self = new NiriActions(owner);
    QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    NiriSession::instance()->ensureStarted();
    return self;
}

NiriActions *NiriActions::instance()
{
    if (!NiriSession::checkThread())
        return nullptr;
    auto *app = QCoreApplication::instance();
    auto *self = app->findChild<NiriActions *>(QStringLiteral("niriqml.actions"),
                                               Qt::FindDirectChildrenOnly);
    if (!self) {
        self = new NiriActions(app);
        self->setObjectName(QStringLiteral("niriqml.actions"));
    }
    NiriSession::instance()->ensureStarted();
    return self;
}

NiriActions::NiriActions(QObject *parent) : NiriIpcActions(parent) { }

NiriPendingReply *NiriActions::sendAction(const QJsonObject &action)
{
    return sendVariantAction(action.toVariantMap());
}

NiriPendingReply *NiriActions::sendVariantAction(const QVariant &action)
{
    if (!NiriSession::checkThread(this))
        return nullptr;
    auto *requests = NiriRequests::instance();
    NiriRequestOperation::Options options;
    options.timeoutMs = requests->requestTimeoutMs();
    options.maxFrameBytes = requests->maxFrameBytes();
    // Use the same checked conversion boundary as raw requests and sequences.
    // Native QVariant integers keep IDs exact; rejected input never reaches I/O.
    QString error;
    const auto request = NiriRequests::wireValue(QVariantMap{{"Action", action}}, error);
    return NiriRequests::dispatch(request.value_or(NiriWire::WireValue()), this, options, nullptr,
                                  {}, error);
}

NiriPendingReply *NiriActions::submitAction(const NiriWire::WireValue &action,
                                            const QString &validationError)
{
    if (!NiriSession::checkThread(this))
        return nullptr;
    const auto *requests = NiriRequests::instance();
    NiriRequestOperation::Options options;
    options.timeoutMs = requests->requestTimeoutMs();
    options.maxFrameBytes = requests->maxFrameBytes();
    const auto decoder = [](const NiriWire::WireValue &ok, QVariant &value, NiriError &error) {
        if (!ok.stringValue() || *ok.stringValue() != QStringLiteral("Handled")) {
            error.fieldPath = QStringLiteral("$.Ok");
            error.message = QStringLiteral("Expected Handled response");
            return false;
        }
        value = QStringLiteral("Handled");
        return true;
    };
    const auto request = *NiriWire::WireValue::object({{QStringLiteral("Action"), action}});
    return NiriRequests::dispatch(request, this, options, nullptr, decoder, validationError);
}

namespace {
template <typename T>
bool legacyArgument(const QVariant &input, T &value, QString &error)
{
    const auto wire = NiriRequests::wireValue(input, error);
    if (!wire)
        return false;
    NiriIpc::DecodeError detail;
    if (NiriIpc::decode(*wire, value, &detail, QStringLiteral("$"), NiriIpc::DecodeMode::Outbound))
        return true;
    error = detail.path + QStringLiteral(": ") + detail.message;
    return false;
}
QVariant legacySize(const QVariant &change)
{
    if (change.typeId() == QMetaType::Double)
        return QVariantMap{{"SetProportion", change}};
    if (change.typeId() == QMetaType::Int)
        return QVariantMap{{"SetFixed", change}};
    return change;
}
} // namespace

NiriPendingReply *NiriActions::moveWindowToWorkspace(quint64 windowId, const QVariant &reference,
                                                     bool focus)
{
    QVariant input = reference;
    if (reference.typeId() == QMetaType::QString)
        input = QVariantMap{{"Name", reference}};
    else if (reference.typeId() == QMetaType::Int)
        input = QVariantMap{{"Index", reference}};
    NiriWorkspaceReferenceArg typed;
    QString error;
    if (!legacyArgument(input, typed, error))
        return submitAction({}, error);
    return NiriIpcActions::moveWindowToWorkspace(std::optional<quint64>(windowId), typed, focus);
}
NiriPendingReply *NiriActions::setWindowWidth(quint64 windowId, const QVariant &change)
{
    NiriSizeChange typed;
    QString error;
    if (!legacyArgument(legacySize(change), typed, error))
        return submitAction({}, error);
    return NiriIpcActions::setWindowWidth(std::optional<quint64>(windowId), typed);
}
NiriPendingReply *NiriActions::setWindowHeight(quint64 windowId, const QVariant &change)
{
    NiriSizeChange typed;
    QString error;
    if (!legacyArgument(legacySize(change), typed, error))
        return submitAction({}, error);
    return NiriIpcActions::setWindowHeight(std::optional<quint64>(windowId), typed);
}
