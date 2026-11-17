#pragma once

#include "nirieventstreamstate.h"
#include <QAbstractListModel>
#include <QPointer>
#include <algorithm>
#include <functional>

// Only ID rows are retained. Before-images are borrowed from the reducer's
// publication transaction, never reduced or retained as projection state.
template <typename T>
class NiriRecordModel : public QAbstractListModel
{
public:
    explicit NiriRecordModel(QObject *parent) : QAbstractListModel(parent) { }

protected:
    using ChangeMap = QMap<quint64, NiriEventStreamState::RecordChange<T>>;
    QList<quint64> m_rows;
    quint64 m_revision = 0;
    const ChangeMap *m_transition = nullptr;

    NiriEventStreamState::Entry<T>
    rowEntry(int row, const std::function<NiriEventStreamState::Entry<T>(quint64)> &get) const
    {
        const auto id = m_rows.at(row);
        if (m_transition) {
            auto it = m_transition->constFind(id);
            if (it != m_transition->cend())
                return it->before ? it->before : it->after;
        }
        return get(id);
    }

    void reconcile(const ChangeMap &changes, quint64 revision,
                   const std::function<QList<int>(quint64)> &roles)
    {
        if (revision <= m_revision)
            return; // Created during this transaction: already at committed state.
        m_revision = revision;
        if (changes.isEmpty())
            return;
        m_transition = &changes;
        QPointer<QAbstractItemModel> guard(this);
        for (auto it = changes.cend(); it != changes.cbegin();) {
            --it;
            if (it->before && !it->after) {
                const int row = std::lower_bound(m_rows.cbegin(), m_rows.cend(), it.key())
                        - m_rows.cbegin();
                beginRemoveRows({}, row, row);
                if (!guard)
                    return;
                m_rows.removeAt(row);
                endRemoveRows();
                if (!guard)
                    return;
            }
        }
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            if (!it->before && it->after) {
                const int row = std::lower_bound(m_rows.cbegin(), m_rows.cend(), it.key())
                        - m_rows.cbegin();
                beginInsertRows({}, row, row);
                if (!guard)
                    return;
                m_rows.insert(row, it.key());
                endInsertRows();
                if (!guard)
                    return;
            }
        }
        m_transition = nullptr;
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            if (it->before && it->after) {
                const int row = std::lower_bound(m_rows.cbegin(), m_rows.cend(), it.key())
                        - m_rows.cbegin();
                emit dataChanged(index(row), index(row), roles(it->fields));
                if (!guard)
                    return;
            }
        }
    }
};

// Notification before-values exist only on the active stack. Nested setters
// commit their selectors and let this owner reconcile against current getters.
// No stale argument or record is ever written back after a callback.
class NiriProjectionNotifications
{
public:
    using Values = QList<QVariant>;
    using Signal = std::function<void()>;
    void flush(QObject *object, Values before, const std::function<Values()> &read,
               const QList<Signal> &notifications)
    {
        if (m_flushing)
            return;
        m_flushing = true;
        QPointer<QObject> guard(object);
        for (;;) {
            const auto now = read();
            int i = 0;
            while (i < now.size() && before[i] == now[i])
                ++i;
            if (i == now.size())
                break;
            before[i] = now[i];
            notifications[i]();
            if (!guard)
                return;
        }
        m_flushing = false;
    }

private:
    bool m_flushing = false;
};
