#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QtQml/qqmlregistration.h>

#include "niristateprojection.h"

class NiriWindowsModel : public NiriRecordModel<NiriWindow>
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriWindows)

public:
    enum Roles {
        WindowRole = Qt::UserRole + 1,
        IdRole,
        TitleRole,
        AppIdRole,
        PidRole,
        WorkspaceIdRole,
        IsFocusedRole,
        IsFloatingRole,
        IsUrgentRole,
        LayoutRole,
        FocusTimestampRole,
    };

    explicit NiriWindowsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

protected:
    QHash<int, QByteArray> roleNames() const override;
};
