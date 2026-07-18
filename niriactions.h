#pragma once

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "niripendingreply.h"

class NiriActions : public QObject
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

    Q_INVOKABLE NiriPendingReply *spawn(const QStringList &command);
    Q_INVOKABLE NiriPendingReply *focusWindow(quint64 windowId);
    Q_INVOKABLE NiriPendingReply *closeWindow(quint64 windowId);
    Q_INVOKABLE NiriPendingReply *moveWindowToWorkspace(quint64 windowId, const QVariant &reference,
                                                        bool followFocus = true);
    Q_INVOKABLE NiriPendingReply *moveWindowToMonitor(quint64 windowId, const QString &output,
                                                      bool followFocus = true);
    Q_INVOKABLE NiriPendingReply *setWindowWidth(quint64 windowId, const QVariant &change);
    Q_INVOKABLE NiriPendingReply *setWindowHeight(quint64 windowId, const QVariant &change);
    Q_INVOKABLE NiriPendingReply *moveColumnToIndex(int index);
    Q_INVOKABLE NiriPendingReply *toggleWindowFloating(quint64 windowId);

private:
    explicit NiriActions(QObject *parent = nullptr);
};
