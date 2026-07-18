#pragma once

#include <QAbstractListModel>
#include <QHash>
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
        IdRole,
        TitleRole,
        AppIdRole,
        PidRole,
        WorkspaceIdRole,
        IsFocusedRole,
        IsFloatingRole,
        IsUrgentRole,
    };

    explicit NiriWindowsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

protected:
    QHash<int, QByteArray> roleNames() const override;

private slots:
    void onWindowsChanged(const QVariantList &windows);
    void onWindowOpenedOrChanged(const NiriWindow &window);
    void onWindowClosed(quint64 id);
    void onWindowFocusChanged(quint64 id);

private:
    QList<NiriWindow> m_windows;
    QHash<quint64, int> m_idToRow;

    void rebuildIndex();
};
