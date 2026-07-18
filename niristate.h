#pragma once

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

#include "niritypes.h"

class NiriState : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(NiriState)

    Q_PROPERTY(quint64 focusedWindowId READ focusedWindowId NOTIFY stateChanged)
    Q_PROPERTY(QString focusedWindowAppId READ focusedWindowAppId NOTIFY stateChanged)
    Q_PROPERTY(QString focusedWindowTitle READ focusedWindowTitle NOTIFY stateChanged)
    Q_PROPERTY(quint64 focusedWorkspaceId READ focusedWorkspaceId NOTIFY stateChanged)
    Q_PROPERTY(QString activeOutput READ activeOutput NOTIFY stateChanged)
    Q_PROPERTY(int windowCount READ windowCount NOTIFY stateChanged)
    Q_PROPERTY(int workspaceCount READ workspaceCount NOTIFY stateChanged)

public:
    static NiriState *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriState *instance();

    quint64 focusedWindowId() const;
    QString focusedWindowAppId() const;
    QString focusedWindowTitle() const;
    quint64 focusedWorkspaceId() const;
    QString activeOutput() const;
    int windowCount() const;
    int workspaceCount() const;

signals:
    void stateChanged();

private slots:
    void onWindowsChanged(const QVariantList &windows);
    void onWindowOpenedOrChanged(const NiriWindow &window);
    void onWindowClosed(quint64 id);
    void onWindowFocusChanged(quint64 id);
    void onWorkspacesChanged(const QVariantList &workspaces);
    void onWorkspaceActivated(quint64 workspaceId, bool focused);

private:
    explicit NiriState(QObject *parent = nullptr);

    void syncFocusedWindowInfo();

    QHash<quint64, NiriWindow> m_windowsById;
    QHash<quint64, NiriWorkspace> m_workspacesById;

    quint64 m_focusedWindowId = 0;
    QString m_focusedWindowAppId;
    QString m_focusedWindowTitle;
    quint64 m_focusedWorkspaceId = 0;
    QString m_activeOutput;
    int m_windowCount = 0;
    int m_workspaceCount = 0;
};
