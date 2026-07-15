#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

#include "niritypes.h"

class NiriWorkspacesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriWorkspaces)

public:
    enum Roles {
        WorkspaceRole = Qt::UserRole + 1,
    };

    explicit NiriWorkspacesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

protected:
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<NiriWorkspace> m_workspaces;
};
