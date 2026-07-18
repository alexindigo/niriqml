#include "niriactions.h"

#include <QJSEngine>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "nirierror.h"
#include "nirirequests.h"

NiriActions *NiriActions::create(QQmlEngine *, QJSEngine *jsEngine)
{
    NiriActions *self = instance();
    if (jsEngine)
        QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    return self;
}

NiriActions *NiriActions::instance()
{
    static NiriActions s;
    return &s;
}

NiriActions::NiriActions(QObject *parent)
    : QObject(parent)
{
}

NiriPendingReply *NiriActions::sendAction(const QJsonObject &action)
{
    auto *reply = new NiriPendingReply(this);
    QJsonObject request;
    request["Action"] = action;

    NiriRequests::instance()->send(request, [reply](bool ok, const QJsonObject &result) {
        if (ok) {
            reply->setFinished(QVariant::fromValue(result.toVariantMap()));
        } else {
            NiriError err;
            err.code = -1;
            err.message = result.value("error").toString();
            reply->setError(err);
        }
    });
    return reply;
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
    return sendAction(QJsonObject{{"FocusWindow",
        QJsonObject{{"id", qint64(windowId)}}}});
}

NiriPendingReply *NiriActions::closeWindow(quint64 windowId)
{
    return sendAction(QJsonObject{{"CloseWindow",
        QJsonObject{{"id", qint64(windowId)}}}});
}

NiriPendingReply *NiriActions::moveWindowToWorkspace(quint64 windowId, const QVariant &reference,
                                                     bool followFocus)
{
    return sendAction(QJsonObject{{"MoveWindowToWorkspace",
        QJsonObject{
            {"window_id", qint64(windowId)},
            {"reference", workspaceReferenceJson(reference)},
            {"focus", followFocus}
        }}});
}

NiriPendingReply *NiriActions::moveWindowToMonitor(quint64 windowId, const QString &output,
                                                    bool followFocus)
{
    return sendAction(QJsonObject{{"MoveWindowToMonitor",
        QJsonObject{
            {"window_id", qint64(windowId)},
            {"output", output},
            {"focus", followFocus}
        }}});
}

NiriPendingReply *NiriActions::setWindowWidth(quint64 windowId, const QVariant &change)
{
    return sendAction(QJsonObject{{"SetWindowWidth",
        QJsonObject{
            {"window_id", qint64(windowId)},
            {"change", sizeChangeJson(change)}
        }}});
}

NiriPendingReply *NiriActions::setWindowHeight(quint64 windowId, const QVariant &change)
{
    return sendAction(QJsonObject{{"SetWindowHeight",
        QJsonObject{
            {"window_id", qint64(windowId)},
            {"change", sizeChangeJson(change)}
        }}});
}

NiriPendingReply *NiriActions::moveColumnToIndex(int index)
{
    return sendAction(QJsonObject{{"MoveColumnToIndex",
        QJsonObject{{"index", index}}}});
}

NiriPendingReply *NiriActions::toggleWindowFloating(quint64 windowId)
{
    return sendAction(QJsonObject{{"ToggleWindowFloating",
        QJsonObject{{"id", qint64(windowId)}}}});
}
