#include "niriworkspacesmodel.h"

NiriWorkspacesModel::NiriWorkspacesModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int NiriWorkspacesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_workspaces.size();
}

QVariant NiriWorkspacesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_workspaces.size())
        return {};

    if (role == WorkspaceRole)
        return QVariant::fromValue(m_workspaces.at(index.row()));

    return {};
}

QHash<int, QByteArray> NiriWorkspacesModel::roleNames() const
{
    return {
        { WorkspaceRole, "workspace" },
    };
}
