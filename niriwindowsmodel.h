#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

#include "niritypes.h"

class NiriWindowsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriWindows)

public:
    enum Roles {
        WindowRole = Qt::UserRole + 1,
    };

    explicit NiriWindowsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

protected:
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<NiriWindow> m_windows;
};
