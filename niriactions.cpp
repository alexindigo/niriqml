#include "niriactions.h"

#include <QJSEngine>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QCoreApplication>
#include "nirisession.h"

#include "nirierror.h"
#include "nirirequests.h"

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

NiriActions::NiriActions(QObject *parent) : QObject(parent) { }

NiriPendingReply *NiriActions::sendAction(const QJsonObject &action)
{
    if (!NiriSession::checkThread(this))
        return nullptr;
    return sendWireAction(*NiriWire::fromQJsonValueLossy(action));
}

static NiriWire::WireValue object(NiriWire::WireValue::Object members)
{
    return *NiriWire::WireValue::object(std::move(members));
}

static NiriWire::WireValue id(quint64 value)
{
    return NiriWire::WireValue(NiriWire::NumberToken::fromUInt64(value));
}

NiriPendingReply *NiriActions::sendWireAction(const NiriWire::WireValue &action)
{
    if (!NiriSession::checkThread(this))
        return nullptr;
    auto *requests = NiriRequests::instance();
    NiriRequestOperation::Options options;
    options.timeoutMs = requests->requestTimeoutMs();
    options.maxFrameBytes = requests->maxFrameBytes();
    return NiriRequests::dispatch(object({{"Action", action}}), this, options);
}

// Convert QVariant reference (string or int) to niri WorkspaceReferenceArg JSON.
// - String → {"Name":"<name>"}
// - Int    → {"Index":<n>}
static QJsonObject workspaceReferenceJson(const QVariant &reference)
{
    if (reference.typeId() == QMetaType::QString) {
        return QJsonObject{{"Name", reference.toString()}};
    }
    return QJsonObject{{"Index", reference.toInt()}};
}

// Convert QVariant size to niri SizeChange JSON.
// Convention: int → SetFixed, double → SetProportion.
// Advanced: pass a QVariantMap with "SetFixed"/"SetProportion"/"AdjustFixed"/"AdjustProportion" key.
static QJsonObject sizeChangeJson(const QVariant &change)
{
    if (change.typeId() == QMetaType::QVariantMap) {
        return QJsonObject::fromVariantMap(change.toMap());
    }
    if (change.typeId() == QMetaType::Double) {
        return QJsonObject{{"SetProportion", change.toDouble()}};
    }
    return QJsonObject{{"SetFixed", change.toInt()}};
}

NiriPendingReply *NiriActions::spawn(const QStringList &command)
{
    QJsonArray cmd;
    for (const QString &s : command)
        cmd.append(s);
    return sendAction(QJsonObject{{"Spawn", QJsonObject{{"command", cmd}}}});
}

NiriPendingReply *NiriActions::focusWindow(quint64 windowId)
{
    return sendWireAction(object({{"FocusWindow", object({{"id", id(windowId)}})}}));
}

NiriPendingReply *NiriActions::closeWindow(quint64 windowId)
{
    return sendWireAction(object({{"CloseWindow", object({{"id", id(windowId)}})}}));
}

NiriPendingReply *NiriActions::moveWindowToWorkspace(quint64 windowId, const QVariant &reference,
                                                     bool followFocus)
{
    return sendWireAction(
            object({{"MoveWindowToWorkspace",
                     object({{"window_id", id(windowId)},
                             {"reference",
                              *NiriWire::fromQJsonValueLossy(workspaceReferenceJson(reference))},
                             {"focus", NiriWire::WireValue(followFocus)}})}}));
}

NiriPendingReply *NiriActions::moveWindowToMonitor(quint64 windowId, const QString &output)
{
    return sendWireAction(
            object({{"MoveWindowToMonitor",
                     object({{"id", id(windowId)}, {"output", NiriWire::WireValue(output)}})}}));
}

NiriPendingReply *NiriActions::setWindowWidth(quint64 windowId, const QVariant &change)
{
    return sendWireAction(object(
            {{"SetWindowWidth",
              object({{"id", id(windowId)},
                      {"change", *NiriWire::fromQJsonValueLossy(sizeChangeJson(change))}})}}));
}

NiriPendingReply *NiriActions::setWindowHeight(quint64 windowId, const QVariant &change)
{
    return sendWireAction(object(
            {{"SetWindowHeight",
              object({{"id", id(windowId)},
                      {"change", *NiriWire::fromQJsonValueLossy(sizeChangeJson(change))}})}}));
}

NiriPendingReply *NiriActions::moveColumnToIndex(int index)
{
    return sendAction(QJsonObject{{"MoveColumnToIndex", QJsonObject{{"index", index}}}});
}

NiriPendingReply *NiriActions::toggleWindowFloating(quint64 windowId)
{
    return sendWireAction(object({{"ToggleWindowFloating", object({{"id", id(windowId)}})}}));
}
