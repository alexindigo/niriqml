#pragma once

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "niripendingreply.h"
#include "generated/niriipcactions_generated.h"

class NiriActions : public NiriIpcActions
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(NiriActions)

public:
    static NiriActions *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriActions *instance();

    // Sends a raw action JSON (advanced usage / testing). Returns a NiriPendingReply.
    // The `action` object is what goes inside {"Action": ...}.
    Q_INVOKABLE NiriPendingReply *sendAction(const QJsonObject &action);

    // Native compatibility adapters. QML uses the checked generated signatures.
    using NiriIpcActions::moveWindowToWorkspace;
    using NiriIpcActions::setWindowHeight;
    using NiriIpcActions::setWindowWidth;
    NiriPendingReply *moveWindowToWorkspace(quint64 windowId, const QVariant &reference,
                                            bool focus);
    NiriPendingReply *setWindowWidth(quint64 windowId, const QVariant &change);
    NiriPendingReply *setWindowHeight(quint64 windowId, const QVariant &change);

private:
    explicit NiriActions(QObject *parent = nullptr);
    NiriPendingReply *sendVariantAction(const QVariant &action);
    NiriHandledReply *submitAction(const NiriWire::WireValue &action,
                                   const QString &validationError) override;
};
