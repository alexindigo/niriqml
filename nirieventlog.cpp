#include "nirieventlog.h"

#include "nirievents.h"

NiriEventLog::NiriEventLog(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(NiriEvents::instance(), &NiriEvents::rawEvent,
            this, &NiriEventLog::appendEvent);
}

int NiriEventLog::capacity() const { return m_capacity; }

void NiriEventLog::setCapacity(int capacity)
{
    if (m_capacity == capacity)
        return;
    m_capacity = capacity;
    while (m_events.size() > m_capacity)
        m_events.removeFirst();
    emit capacityChanged();
}

int NiriEventLog::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_events.size();
}

QVariant NiriEventLog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_events.size())
        return {};

    const auto &event = m_events.at(index.row());
    switch (role) {
    case NameRole:
        return event.name;
    case PayloadRole:
        return QVariant(event.payload);
    default:
        return {};
    }
}

QHash<int, QByteArray> NiriEventLog::roleNames() const
{
    return {
        { NameRole, "name" },
        { PayloadRole, "payload" },
    };
}

void NiriEventLog::appendEvent(const QString &name, const QVariantMap &payload)
{
    if (m_events.size() >= m_capacity)
        m_events.removeFirst();

    beginInsertRows(QModelIndex(), m_events.size(), m_events.size());
    m_events.append({name, payload});
    endInsertRows();
}
