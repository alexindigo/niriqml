#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "niritypes.h"

class NiriWorkspaceReactive : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriWorkspace)

    // Selector: match by name (preferred) or id
    Q_PROPERTY(QString workspaceName READ workspaceName WRITE setWorkspaceName NOTIFY
                       workspaceNameChanged)
    Q_PROPERTY(QVariant workspaceId READ qmlWorkspaceId WRITE setQmlWorkspaceId NOTIFY
                       workspaceIdChanged)

    Q_PROPERTY(bool valid READ valid NOTIFY workspaceChanged)
    Q_PROPERTY(QString id READ qmlId NOTIFY workspaceChanged)
    Q_PROPERTY(int idx READ idx NOTIFY workspaceChanged)
    Q_PROPERTY(QVariant name READ qmlName NOTIFY workspaceChanged)
    Q_PROPERTY(QVariant output READ qmlOutput NOTIFY workspaceChanged)
    Q_PROPERTY(bool isUrgent READ isUrgent NOTIFY workspaceChanged)
    Q_PROPERTY(bool isActive READ isActive NOTIFY workspaceChanged)
    Q_PROPERTY(bool isFocused READ isFocused NOTIFY workspaceChanged)
    Q_PROPERTY(QVariant activeWindowId READ qmlActiveWindowId NOTIFY workspaceChanged)

public:
    explicit NiriWorkspaceReactive(QObject *parent = nullptr);

    QString workspaceName() const;
    void setWorkspaceName(const QString &name);
    std::optional<quint64> workspaceId() const;
    void setWorkspaceId(std::optional<quint64> id);

    bool valid() const;
    quint64 id() const;
    int idx() const;
    std::optional<QString> name() const;
    std::optional<QString> output() const;
    bool isUrgent() const;
    bool isActive() const;
    bool isFocused() const;
    std::optional<quint64> activeWindowId() const;

    QVariant qmlWorkspaceId() const { return NiriIpc::qmlValue(workspaceId()); }
    void setQmlWorkspaceId(const QVariant &id);
    QString qmlId() const { return QString::number(this->id()); }
    QVariant qmlName() const { return NiriIpc::qmlValue(name()); }
    QVariant qmlOutput() const { return NiriIpc::qmlValue(output()); }
    QVariant qmlActiveWindowId() const { return NiriIpc::qmlValue(activeWindowId()); }

signals:
    void workspaceNameChanged();
    void workspaceIdChanged();
    void workspaceChanged();

private slots:
    void onWorkspacesChanged(const QVariantList &workspaces);
    void onWorkspaceActivated(quint64 workspaceId, bool focused);
    void onWorkspaceActiveWindowChanged(quint64 workspaceId, std::optional<quint64> windowId);

private:
    bool matches(const NiriWorkspace &w) const;
    void updateFrom(const NiriWorkspace &w);
    void clear();

    QString m_selectorName;
    std::optional<quint64> m_selectorId;
    bool m_valid = false;
    NiriWorkspace m_data;
};
