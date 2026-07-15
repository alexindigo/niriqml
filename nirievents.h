#pragma once

#include <QObject>
#include <QVariantMap>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

#include "niritypes.h"

class NiriEvents : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(NiriEvents)

public:
    static NiriEvents *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriEvents *instance();

    void dispatchEvent(const QString &name, const QVariantMap &payload);

signals:
    void workspacesChanged(const QVariantList &workspaces);
    void workspaceActivated(quint64 workspaceId, const QString &output);
    void workspaceActiveWindowChanged(quint64 workspaceId, quint64 windowId);
    void windowsChanged(const QVariantList &windows);
    void windowOpenedOrChanged(const NiriWindow &window);
    void windowClosed(quint64 windowId);
    void windowFocusChanged(quint64 windowId);
    void windowLayoutsChanged(const QVariantList &changes);
    void keyboardLayoutsChanged(const QVariantList &layouts);
    void keyboardLayoutSwitched(int idx);
    void overviewOpenedOrClosed(bool isOpen);

    void rawEvent(const QString &name, const QVariantMap &payload);

private:
    explicit NiriEvents(QObject *parent = nullptr);
};
