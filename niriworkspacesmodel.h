#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QtQml/qqmlregistration.h>

#include "niritypes.h"

class NiriWorkspacesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriWorkspaces)

public:
    enum Roles {
        WorkspaceRole = Qt::UserRole + 1,
        IdRole,
        IdxRole,
        NameRole,
        OutputRole,
        IsUrgentRole,
        IsActiveRole,
        IsFocusedRole,
        ActiveWindowIdRole,
    };

    explicit NiriWorkspacesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

protected:
    QHash<int, QByteArray> roleNames() const override;

private slots:
    void onWorkspacesChanged(const QVariantList &workspaces);
    void onWorkspaceActivated(quint64 workspaceId, bool focused);
    void onWorkspaceActiveWindowChanged(quint64 workspaceId, quint64 windowId);

private:
    QList<NiriWorkspace> m_workspaces;
    QHash<quint64, int> m_idToRow;

    void rebuildIndex();
};
