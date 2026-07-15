#pragma once

#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QQmlEngine>
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

    Q_INVOKABLE NiriPendingReply *spawn(const QStringList &command);
    Q_INVOKABLE NiriPendingReply *moveWindowToWorkspace(quint64 windowId, const QVariant &reference);
    Q_INVOKABLE NiriPendingReply *moveWindowToMonitor(quint64 windowId, const QString &monitor);
    Q_INVOKABLE NiriPendingReply *focusWindow(quint64 windowId);
    Q_INVOKABLE NiriPendingReply *closeWindow(quint64 windowId);
    Q_INVOKABLE NiriPendingReply *setWindowWidth(quint64 windowId, const QVariant &size);
    Q_INVOKABLE NiriPendingReply *setWindowHeight(quint64 windowId, const QVariant &size);
    Q_INVOKABLE NiriPendingReply *moveColumnToIndex(int index);
    Q_INVOKABLE NiriPendingReply *toggleWindowFloating(quint64 windowId);

private:
    explicit NiriActions(QObject *parent = nullptr);
};
