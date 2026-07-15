#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

class NiriState : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(NiriState)

    Q_PROPERTY(quint64 focusedWindowId READ focusedWindowId NOTIFY stateChanged)
    Q_PROPERTY(QString focusedWindowAppId READ focusedWindowAppId NOTIFY stateChanged)
    Q_PROPERTY(quint64 focusedWorkspaceId READ focusedWorkspaceId NOTIFY stateChanged)
    Q_PROPERTY(QString activeOutput READ activeOutput NOTIFY stateChanged)
    Q_PROPERTY(int windowCount READ windowCount NOTIFY stateChanged)
    Q_PROPERTY(int workspaceCount READ workspaceCount NOTIFY stateChanged)

public:
    static NiriState *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriState *instance();

    quint64 focusedWindowId() const;
    QString focusedWindowAppId() const;
    quint64 focusedWorkspaceId() const;
    QString activeOutput() const;
    int windowCount() const;
    int workspaceCount() const;

signals:
    void stateChanged();

private:
    explicit NiriState(QObject *parent = nullptr);

    quint64 m_focusedWindowId = 0;
    QString m_focusedWindowAppId;
    quint64 m_focusedWorkspaceId = 0;
    QString m_activeOutput;
    int m_windowCount = 0;
    int m_workspaceCount = 0;
};
