#include "niriworkspacesmodel.h"

#include "nirievents.h"

NiriWorkspacesModel::NiriWorkspacesModel(QObject *parent) : QAbstractListModel(parent)
{
    NiriEvents *events = NiriEvents::instance();
    connect(events, &NiriEvents::workspacesChanged, this,
            &NiriWorkspacesModel::onWorkspacesChanged);
    connect(events, &NiriEvents::workspaceActivated, this,
            &NiriWorkspacesModel::onWorkspaceActivated);
    connect(events, &NiriEvents::workspaceActiveWindowChanged, this,
            &NiriWorkspacesModel::onWorkspaceActiveWindowChanged);

    // Bootstrap from cached snapshot for late subscribers
    QVariantList snapshot = events->lastWorkspacesSnapshot();
    if (!snapshot.isEmpty())
        onWorkspacesChanged(snapshot);
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
        return { };

    const NiriWorkspace &w = m_workspaces.at(index.row());
    switch (role) {
    case WorkspaceRole:
        return QVariant::fromValue(w);
    case IdRole:
        return QVariant::fromValue(w.id);
    case IdxRole:
        return w.idx;
    case NameRole:
        return w.name;
    case OutputRole:
        return w.output;
    case IsUrgentRole:
        return w.isUrgent;
    case IsActiveRole:
        return w.isActive;
    case IsFocusedRole:
        return w.isFocused;
    case ActiveWindowIdRole:
        return QVariant::fromValue(w.activeWindowId);
    default:
        return { };
    }
}

QHash<int, QByteArray> NiriWorkspacesModel::roleNames() const
{
    return {
        { WorkspaceRole, "workspace" },
        { IdRole, "id" },
        { IdxRole, "idx" },
        { NameRole, "name" },
        { OutputRole, "output" },
        { IsUrgentRole, "isUrgent" },
        { IsActiveRole, "isActive" },
        { IsFocusedRole, "isFocused" },
        { ActiveWindowIdRole, "activeWindowId" },
    };
}

void NiriWorkspacesModel::rebuildIndex()
{
    m_idToRow.clear();
    for (int i = 0; i < m_workspaces.size(); ++i)
        m_idToRow[m_workspaces[i].id] = i;
}

void NiriWorkspacesModel::onWorkspacesChanged(const QVariantList &workspaces)
{
    beginResetModel();
    m_workspaces.clear();
    m_workspaces.reserve(workspaces.size());
    for (const QVariant &v : workspaces)
        m_workspaces.append(v.value<NiriWorkspace>());
    rebuildIndex();
    endResetModel();
}

void NiriWorkspacesModel::onWorkspaceActivated(quint64 workspaceId, bool focused)
{
    // Look up the target workspace to find its output
    auto it = m_idToRow.constFind(workspaceId);
    if (it == m_idToRow.constEnd())
        return;
    const QString targetOutput = m_workspaces.at(*it).output;

    // Update isActive: on the target output, exactly one workspace is active.
    // If `focused`, also update isFocused so the model reflects focus state.
    for (int i = 0; i < m_workspaces.size(); ++i) {
        NiriWorkspace &w = m_workspaces[i];
        if (w.output != targetOutput)
            continue;

        bool wasActive = w.isActive;
        bool nowActive = (w.id == workspaceId);
        bool wasFocused = w.isFocused;
        bool nowFocused = focused ? (w.id == workspaceId) : wasFocused;

        QList<int> changedRoles;
        if (wasActive != nowActive) {
            w.isActive = nowActive;
            changedRoles << IsActiveRole;
        }
        if (wasFocused != nowFocused) {
            w.isFocused = nowFocused;
            changedRoles << IsFocusedRole;
        }
        if (!changedRoles.isEmpty()) {
            changedRoles << WorkspaceRole;
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, changedRoles);
        }
    }
}

void NiriWorkspacesModel::onWorkspaceActiveWindowChanged(quint64 workspaceId, quint64 windowId)
{
    auto it = m_idToRow.constFind(workspaceId);
    if (it == m_idToRow.constEnd())
        return;
    int row = *it;
    m_workspaces[row].activeWindowId = windowId;
    QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { ActiveWindowIdRole, WorkspaceRole });
}
