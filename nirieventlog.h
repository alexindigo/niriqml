#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QQueue>
#include <functional>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class NiriEventLog : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriEventLog)

    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY capacityChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PayloadRole,
    };

    explicit NiriEventLog(QObject *parent = nullptr);

    int capacity() const;
    void setCapacity(int capacity);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void appendEvent(const QString &name, const QVariant &payload);

signals:
    void capacityChanged();

protected:
    QHash<int, QByteArray> roleNames() const override;

private:
    int m_capacity = 500;
    struct Event
    {
        QString name;
        QVariant payload;
    };
    QList<Event> m_events;
    QQueue<std::function<void()>> m_pending;
    bool m_mutating = false;
    void enqueue(std::function<void()> operation);
    void removeOldest(int count);
};
