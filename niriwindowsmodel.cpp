#include "niriwindowsmodel.h"

NiriWindowsModel::NiriWindowsModel(QObject *parent)
    : QAbstractListModel(parent)
{
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

    if (role == WindowRole)
        return QVariant::fromValue(m_windows.at(index.row()));

    return {};
}

QHash<int, QByteArray> NiriWindowsModel::roleNames() const
{
    return {
        { WindowRole, "window" },
    };
}
