#pragma once

#include <QAbstractListModel>
#include <QList>
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

    void appendEvent(const QString &name, const QVariantMap &payload);

signals:
    void capacityChanged();

protected:
    QHash<int, QByteArray> roleNames() const override;

private:
    int m_capacity = 500;
    struct Event {
        QString name;
        QVariantMap payload;
    };
    QList<Event> m_events;
};
