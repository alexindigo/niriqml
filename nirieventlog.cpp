#include "nirieventlog.h"

#include "nirievents.h"
#include "nirisession.h"
#include <QPointer>

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
    enqueue([this, capacity = qMax(0, capacity)] {
        if (m_capacity == capacity)
            return;
        // Keep the old capacity valid through removal callbacks, then commit
        // the new value before capacityChanged. Nested writes are serialized.
        QPointer<NiriEventLog> guard(this);
        removeOldest(qMax(0, int(m_events.size()) - capacity));
        if (!guard)
            return;
        m_capacity = capacity;
        emit capacityChanged();
    });
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
    enqueue([this, name, payload] {
        if (!m_capacity)
            return;
        QPointer<NiriEventLog> guard(this);
        if (m_events.size() >= m_capacity)
            removeOldest(1);
        if (!guard)
            return;
        beginInsertRows({}, m_events.size(), m_events.size());
        if (!guard)
            return;
        m_events.append({name, payload});
        endInsertRows();
    });
}

void NiriEventLog::removeOldest(int count)
{
    if (!count)
        return;
    QPointer<NiriEventLog> guard(this);
    beginRemoveRows({}, 0, count - 1);
    if (!guard)
        return;
    m_events.remove(0, count);
    endRemoveRows();
}

void NiriEventLog::enqueue(std::function<void()> operation)
{
    m_pending.enqueue(std::move(operation));
    if (m_mutating)
        return;
    m_mutating = true;
    QPointer<NiriEventLog> guard(this);
    while (!m_pending.isEmpty()) {
        auto next = m_pending.dequeue();
        next();
        if (!guard)
            return;
    }
    m_mutating = false;
}
