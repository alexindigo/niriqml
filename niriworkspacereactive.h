#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class NiriWorkspaceReactive : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriWorkspace)

    Q_PROPERTY(QString workspaceName READ workspaceName WRITE setWorkspaceName NOTIFY workspaceNameChanged)
    Q_PROPERTY(quint64 id READ id NOTIFY workspaceChanged)
    Q_PROPERTY(int idx READ idx NOTIFY workspaceChanged)
    Q_PROPERTY(QString name READ name NOTIFY workspaceChanged)
    Q_PROPERTY(bool isActive READ isActive NOTIFY workspaceChanged)

public:
    explicit NiriWorkspaceReactive(QObject *parent = nullptr);

    QString workspaceName() const;
    void setWorkspaceName(const QString &name);

    quint64 id() const;
    int idx() const;
    QString name() const;
    bool isActive() const;

signals:
    void workspaceNameChanged();
    void workspaceChanged();

private:
    QString m_workspaceName;
    quint64 m_id = 0;
    int m_idx = 0;
    QString m_name;
    bool m_isActive = false;
};
