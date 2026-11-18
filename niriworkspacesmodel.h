#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QtQml/qqmlregistration.h>

#include "niristateprojection.h"

class NiriWorkspacesModel : public NiriRecordModel<NiriWorkspace>
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriWorkspaces)

public:
    enum Roles {
        WorkspaceRole = Qt::UserRole + 1,
        IdRole,
        IdxRole,
        NameRole,
        OutputRole,
        IsUrgentRole,
        IsActiveRole,
        IsFocusedRole,
        ActiveWindowIdRole,
    };

    explicit NiriWorkspacesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

protected:
    QHash<int, QByteArray> roleNames() const override;
};
