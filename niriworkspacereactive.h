#pragma once

#include "niristateprojection.h"
#include <QtQml/qqmlregistration.h>

class NiriWorkspaceReactive : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriWorkspace)
    Q_PROPERTY(QString workspaceName READ workspaceName WRITE setWorkspaceName NOTIFY
                       workspaceNameChanged)
    Q_PROPERTY(QVariant workspaceId READ qmlWorkspaceId WRITE setQmlWorkspaceId NOTIFY
                       workspaceIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(QVariant id READ qmlId NOTIFY idChanged)
    Q_PROPERTY(int idx READ idx NOTIFY idxChanged)
    Q_PROPERTY(QVariant name READ qmlName NOTIFY nameChanged)
    Q_PROPERTY(QVariant output READ qmlOutput NOTIFY outputChanged)
    Q_PROPERTY(bool isUrgent READ isUrgent NOTIFY isUrgentChanged)
    Q_PROPERTY(bool isActive READ isActive NOTIFY isActiveChanged)
    Q_PROPERTY(bool isFocused READ isFocused NOTIFY isFocusedChanged)
    Q_PROPERTY(QVariant activeWindowId READ qmlActiveWindowId NOTIFY activeWindowIdChanged)
    Q_PROPERTY(QVariant workspace READ qmlWorkspace NOTIFY workspaceChanged)

public:
    explicit NiriWorkspaceReactive(QObject *parent = nullptr);
    QString workspaceName() const;
    void setWorkspaceName(const QString &name);
    std::optional<quint64> workspaceId() const;
    void setWorkspaceId(std::optional<quint64> id);
    void setQmlWorkspaceId(const QVariant &id);
    bool valid() const;
    quint64 id() const;
    int idx() const;
    std::optional<QString> name() const;
    std::optional<QString> output() const;
    bool isUrgent() const;
    bool isActive() const;
    bool isFocused() const;
    std::optional<quint64> activeWindowId() const;
    std::optional<NiriWorkspace> workspace() const;
    QVariant qmlWorkspaceId() const { return NiriIpc::qmlValue(workspaceId()); }
    QVariant qmlId() const
    {
        return valid() ? NiriIpc::qmlValue(id()) : QVariant::fromValue(nullptr);
    }
    QVariant qmlName() const { return NiriIpc::qmlValue(name()); }
    QVariant qmlOutput() const { return NiriIpc::qmlValue(output()); }
    QVariant qmlActiveWindowId() const { return NiriIpc::qmlValue(activeWindowId()); }
    QVariant qmlWorkspace() const { return NiriIpc::qmlValue(workspace()); }

signals:
    void workspaceNameChanged();
    void workspaceIdChanged();
    void validChanged();
    void idChanged();
    void idxChanged();
    void nameChanged();
    void outputChanged();
    void isUrgentChanged();
    void isActiveChanged();
    void isFocusedChanged();
    void activeWindowIdChanged();
    void workspaceChanged();

private:
    using Entry = NiriEventStreamState::Entry<NiriWorkspace>;
    Entry selected() const;
    NiriProjectionNotifications::Values values(const Entry &entry) const;
    void notify(NiriProjectionNotifications::Values before);
    QString m_selectorName;
    std::optional<quint64> m_selectorId;
    quint64 m_revision = 0;
    NiriProjectionNotifications m_notifications;
};
