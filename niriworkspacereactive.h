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
    Q_PROPERTY(QString workspaceName READ workspaceName WRITE setWorkspaceName NOTIFY workspaceNameChanged)
    Q_PROPERTY(quint64 workspaceId READ workspaceId WRITE setWorkspaceId NOTIFY workspaceIdChanged)

    Q_PROPERTY(bool valid READ valid NOTIFY workspaceChanged)
    Q_PROPERTY(quint64 id READ id NOTIFY workspaceChanged)
    Q_PROPERTY(int idx READ idx NOTIFY workspaceChanged)
    Q_PROPERTY(QString name READ name NOTIFY workspaceChanged)
    Q_PROPERTY(QString output READ output NOTIFY workspaceChanged)
    Q_PROPERTY(bool isUrgent READ isUrgent NOTIFY workspaceChanged)
    Q_PROPERTY(bool isActive READ isActive NOTIFY workspaceChanged)
    Q_PROPERTY(bool isFocused READ isFocused NOTIFY workspaceChanged)
    Q_PROPERTY(quint64 activeWindowId READ activeWindowId NOTIFY workspaceChanged)

public:
    explicit NiriWorkspaceReactive(QObject *parent = nullptr);

    QString workspaceName() const;
    void setWorkspaceName(const QString &name);
    quint64 workspaceId() const;
    void setWorkspaceId(quint64 id);

    bool valid() const;
    quint64 id() const;
    int idx() const;
    QString name() const;
    QString output() const;
    bool isUrgent() const;
    bool isActive() const;
    bool isFocused() const;
    quint64 activeWindowId() const;

signals:
    void workspaceNameChanged();
    void workspaceIdChanged();
    void workspaceChanged();

private slots:
    void onWorkspacesChanged(const QVariantList &workspaces);
    void onWorkspaceActivated(quint64 workspaceId, bool focused);
    void onWorkspaceActiveWindowChanged(quint64 workspaceId, quint64 windowId);

private:
    bool matches(const NiriWorkspace &w) const;
    void updateFrom(const NiriWorkspace &w);
    void clear();

    QString m_selectorName;
    quint64 m_selectorId = 0;
    bool m_valid = false;
    NiriWorkspace m_data;
};
