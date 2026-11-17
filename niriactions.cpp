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
    return sendVariantAction(
            QVariantMap{{"FocusWindow", QVariantMap{{"id", QVariant::fromValue(windowId)}}}});
}

NiriPendingReply *NiriActions::closeWindow(quint64 windowId)
{
    return sendVariantAction(
            QVariantMap{{"CloseWindow", QVariantMap{{"id", QVariant::fromValue(windowId)}}}});
}

NiriPendingReply *NiriActions::moveWindowToWorkspace(quint64 windowId, const QVariant &reference,
                                                     bool followFocus)
{
    return sendVariantAction(QVariantMap{
            {"MoveWindowToWorkspace",
             QVariantMap{{"window_id", QVariant::fromValue(windowId)},
                         {"reference", workspaceReferenceJson(reference).toVariantMap()},
                         {"focus", followFocus}}}});
}

NiriPendingReply *NiriActions::moveWindowToMonitor(quint64 windowId, const QString &output)
{
    return sendVariantAction(
            QVariantMap{{"MoveWindowToMonitor",
                         QVariantMap{{"id", QVariant::fromValue(windowId)}, {"output", output}}}});
}

NiriPendingReply *NiriActions::setWindowWidth(quint64 windowId, const QVariant &change)
{
    return sendVariantAction(
            QVariantMap{{"SetWindowWidth",
                         QVariantMap{{"id", QVariant::fromValue(windowId)},
                                     {"change", sizeChangeJson(change).toVariantMap()}}}});
}

NiriPendingReply *NiriActions::setWindowHeight(quint64 windowId, const QVariant &change)
{
    return sendVariantAction(
            QVariantMap{{"SetWindowHeight",
                         QVariantMap{{"id", QVariant::fromValue(windowId)},
                                     {"change", sizeChangeJson(change).toVariantMap()}}}});
}

NiriPendingReply *NiriActions::moveColumnToIndex(int index)
{
    return sendAction(QJsonObject{{"MoveColumnToIndex", QJsonObject{{"index", index}}}});
}

NiriPendingReply *NiriActions::toggleWindowFloating(quint64 windowId)
{
    return sendVariantAction(QVariantMap{
            {"ToggleWindowFloating", QVariantMap{{"id", QVariant::fromValue(windowId)}}}});
}
