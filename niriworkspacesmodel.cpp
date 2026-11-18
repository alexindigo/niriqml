#include "niriworkspacesmodel.h"
#include "nirisession.h"

NiriWorkspacesModel::NiriWorkspacesModel(QObject *parent) : NiriRecordModel(parent)
{
    auto *session = NiriSession::instance();
    if (!session)
        return;
    m_rows = session->state().workspaceIds();
    m_revision = session->state().revision();
    connect(session, &NiriSession::stateProject, this, [this](const auto &c) {
        reconcile(c.workspaces, c.revision, [](quint64 fields) {
            QList<int> roles{WorkspaceRole};
            using S = NiriEventStreamState;
            for (const auto &pair :
                 {qMakePair(S::Index, IdxRole), qMakePair(S::Name, NameRole),
                  qMakePair(S::Output, OutputRole), qMakePair(S::WorkspaceUrgent, IsUrgentRole),
                  qMakePair(S::Active, IsActiveRole), qMakePair(S::WorkspaceFocused, IsFocusedRole),
                  qMakePair(S::ActiveWindowId, ActiveWindowIdRole)}) {
                if (fields & pair.first)
                    roles.append(pair.second);
            }
            return roles;
        });
    });
    session->ensureStarted();
}

int NiriWorkspacesModel::rowCount(const QModelIndex &parent) const
{
    return NiriSession::checkThread(this) && !parent.isValid() ? m_rows.size() : 0;
}

QVariant NiriWorkspacesModel::data(const QModelIndex &index, int role) const
{
    if (!NiriSession::checkThread(this) || !index.isValid() || index.model() != this
        || index.column() != 0 || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const auto entry = rowEntry(
            index.row(), [](quint64 id) { return NiriSession::instance()->state().workspace(id); });
    if (!entry)
        return {};
    const auto &w = *entry;
    switch (role) {
    case WorkspaceRole:
        return QVariant::fromValue(w);
    case IdRole:
        return w.qmlId();
    case IdxRole:
        return w.idx;
    case NameRole:
        return w.qmlName();
    case OutputRole:
        return w.qmlOutput();
    case IsUrgentRole:
        return w.isUrgent;
    case IsActiveRole:
        return w.isActive;
    case IsFocusedRole:
        return w.isFocused;
    case ActiveWindowIdRole:
        return w.qmlActiveWindowId();
    default:
        return {};
    }
}

QHash<int, QByteArray> NiriWorkspacesModel::roleNames() const
{
    return {{WorkspaceRole, "workspace"},
            {IdRole, "id"},
            {IdxRole, "idx"},
            {NameRole, "name"},
            {OutputRole, "output"},
            {IsUrgentRole, "isUrgent"},
            {IsActiveRole, "isActive"},
            {IsFocusedRole, "isFocused"},
            {ActiveWindowIdRole, "activeWindowId"}};
}
