#include "niriwindowsmodel.h"

#include "nirievents.h"

NiriWindowsModel::NiriWindowsModel(QObject *parent) : QAbstractListModel(parent)
{
    NiriEvents *events = NiriEvents::instance();
    connect(events, &NiriEvents::windowsChanged, this, &NiriWindowsModel::onWindowsChanged);
    connect(events, &NiriEvents::windowOpenedOrChanged, this,
            &NiriWindowsModel::onWindowOpenedOrChanged);
    connect(events, &NiriEvents::windowClosed, this, &NiriWindowsModel::onWindowClosed);
    connect(events, &NiriEvents::windowFocusChanged, this, &NiriWindowsModel::onWindowFocusChanged);

    // Bootstrap from cached snapshot for late subscribers
    QVariantList snapshot = events->lastWindowsSnapshot();
    if (!snapshot.isEmpty())
        onWindowsChanged(snapshot);
}

int NiriWindowsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_windows.size();
}

QVariant NiriWindowsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_windows.size())
        return {};

    const NiriWindow &w = m_windows.at(index.row());
    switch (role) {
    case WindowRole:
        return QVariant::fromValue(w);
    case IdRole:
        return QVariant::fromValue(w.id);
    case TitleRole:
        return w.title;
    case AppIdRole:
        return w.appId;
    case PidRole:
        return w.pid;
    case WorkspaceIdRole:
        return QVariant::fromValue(w.workspaceId);
    case IsFocusedRole:
        return w.isFocused;
    case IsFloatingRole:
        return w.isFloating;
    case IsUrgentRole:
        return w.isUrgent;
    default:
        return {};
    }
}

QHash<int, QByteArray> NiriWindowsModel::roleNames() const
{
    return {
            {WindowRole, "window"},
            {IdRole, "id"},
            {TitleRole, "title"},
            {AppIdRole, "appId"},
            {PidRole, "pid"},
            {WorkspaceIdRole, "workspaceId"},
            {IsFocusedRole, "isFocused"},
            {IsFloatingRole, "isFloating"},
            {IsUrgentRole, "isUrgent"},
    };
}

void NiriWindowsModel::rebuildIndex()
{
    m_idToRow.clear();
    for (int i = 0; i < m_windows.size(); ++i)
        m_idToRow[m_windows[i].id] = i;
}

void NiriWindowsModel::onWindowsChanged(const QVariantList &windows)
{
    beginResetModel();
    m_windows.clear();
    m_windows.reserve(windows.size());
    for (const QVariant &v : windows)
        m_windows.append(v.value<NiriWindow>());
    rebuildIndex();
    endResetModel();
}

void NiriWindowsModel::onWindowOpenedOrChanged(const NiriWindow &window)
{
    auto it = m_idToRow.constFind(window.id);
    if (it == m_idToRow.constEnd()) {
        int row = m_windows.size();
        beginInsertRows(QModelIndex(), row, row);
        m_windows.append(window);
        m_idToRow[window.id] = row;
        endInsertRows();
    } else {
        int row = *it;
        m_windows[row] = window;
        QModelIndex idx = index(row);
        emit dataChanged(idx, idx);
    }
}

void NiriWindowsModel::onWindowClosed(quint64 id)
{
    auto it = m_idToRow.constFind(id);
    if (it == m_idToRow.constEnd())
        return;
    int row = *it;
    beginRemoveRows(QModelIndex(), row, row);
    m_windows.removeAt(row);
    m_idToRow.remove(id);
    // Rows after `row` shifted down by 1
    for (int i = row; i < m_windows.size(); ++i)
        m_idToRow[m_windows[i].id] = i;
    endRemoveRows();
}

void NiriWindowsModel::onWindowFocusChanged(quint64 id)
{
    // Update isFocused flag on all rows; emit dataChanged for changed rows only
    for (int i = 0; i < m_windows.size(); ++i) {
        bool wasFocused = m_windows[i].isFocused;
        bool nowFocused = (m_windows[i].id == id);
        if (wasFocused != nowFocused) {
            m_windows[i].isFocused = nowFocused;
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {IsFocusedRole, WindowRole});
        }
    }
}
