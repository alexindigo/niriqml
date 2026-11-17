#include "nirieventlog.h"

#include "nirievents.h"
#include "nirisession.h"

NiriEventLog::NiriEventLog(QObject *parent) : QAbstractListModel(parent)
{
    auto *events = NiriEvents::instance();
    if (!events)
        return;
    connect(events, &NiriEvents::rawEvent, this, &NiriEventLog::appendEvent);
    NiriSession::instance()->ensureStarted();
}

int NiriEventLog::capacity() const
{
    return NiriSession::checkThread(this) ? m_capacity : 0;
}

void NiriEventLog::setCapacity(int capacity)
{
    if (!NiriSession::checkThread(this))
        return;
    if (m_capacity == capacity)
        return;
    m_capacity = capacity;
    while (m_events.size() > m_capacity)
        m_events.removeFirst();
    emit capacityChanged();
}

int NiriEventLog::rowCount(const QModelIndex &parent) const
{
    if (!NiriSession::checkThread(this))
        return 0;
    if (parent.isValid())
        return 0;
    return m_events.size();
}

QVariant NiriEventLog::data(const QModelIndex &index, int role) const
{
    if (!NiriSession::checkThread(this))
        return {};
    if (!index.isValid() || index.row() < 0 || index.row() >= m_events.size())
        return {};

    const auto &event = m_events.at(index.row());
    switch (role) {
    case NameRole:
        return event.name;
    case PayloadRole:
        return event.payload;
    default:
        return {};
    }
}

QHash<int, QByteArray> NiriEventLog::roleNames() const
{
    return {
            {NameRole, "name"},
            {PayloadRole, "payload"},
    };
}

void NiriEventLog::appendEvent(const QString &name, const QVariant &payload)
{
    if (!NiriSession::checkThread(this))
        return;
    if (m_events.size() >= m_capacity)
        m_events.removeFirst();

    beginInsertRows(QModelIndex(), m_events.size(), m_events.size());
    m_events.append({name, payload});
    endInsertRows();
}
