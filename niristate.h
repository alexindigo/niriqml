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

    Q_PROPERTY(QVariant focusedWindowId READ qmlFocusedWindowId NOTIFY stateChanged)
    Q_PROPERTY(QVariant focusedWindowAppId READ qmlFocusedWindowAppId NOTIFY stateChanged)
    Q_PROPERTY(QVariant focusedWindowTitle READ qmlFocusedWindowTitle NOTIFY stateChanged)
    Q_PROPERTY(QVariant focusedWorkspaceId READ qmlFocusedWorkspaceId NOTIFY stateChanged)
    Q_PROPERTY(QVariant activeOutput READ qmlActiveOutput NOTIFY stateChanged)
    Q_PROPERTY(int windowCount READ windowCount NOTIFY stateChanged)
    Q_PROPERTY(int workspaceCount READ workspaceCount NOTIFY stateChanged)

public:
    static NiriState *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriState *instance();

    std::optional<quint64> focusedWindowId() const;
    std::optional<QString> focusedWindowAppId() const;
    std::optional<QString> focusedWindowTitle() const;
    std::optional<quint64> focusedWorkspaceId() const;
    std::optional<QString> activeOutput() const;
    QVariant qmlFocusedWindowId() const { return NiriIpc::qmlValue(focusedWindowId()); }
    QVariant qmlFocusedWindowAppId() const { return NiriIpc::qmlValue(focusedWindowAppId()); }
    QVariant qmlFocusedWindowTitle() const { return NiriIpc::qmlValue(focusedWindowTitle()); }
    QVariant qmlFocusedWorkspaceId() const { return NiriIpc::qmlValue(focusedWorkspaceId()); }
    QVariant qmlActiveOutput() const { return NiriIpc::qmlValue(activeOutput()); }
    int windowCount() const;
    int workspaceCount() const;

signals:
    void stateChanged();

private slots:
    void onWindowsChanged(const QVariantList &windows);
    void onWindowOpenedOrChanged(const NiriWindow &window);
    void onWindowClosed(quint64 id);
    void onWindowFocusChanged(std::optional<quint64> id);
    void onWorkspacesChanged(const QVariantList &workspaces);
    void onWorkspaceActivated(quint64 workspaceId, bool focused);

private:
    explicit NiriState(QObject *parent = nullptr);

    void syncFocusedWindowInfo();

    QHash<quint64, NiriWindow> m_windowsById;
    QHash<quint64, NiriWorkspace> m_workspacesById;

    std::optional<quint64> m_focusedWindowId;
    std::optional<QString> m_focusedWindowAppId;
    std::optional<QString> m_focusedWindowTitle;
    std::optional<quint64> m_focusedWorkspaceId;
    std::optional<QString> m_activeOutput;
    int m_windowCount = 0;
    int m_workspaceCount = 0;
};
