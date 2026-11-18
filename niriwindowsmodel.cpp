#include "niriwindowsmodel.h"
#include "nirisession.h"

NiriWindowsModel::NiriWindowsModel(QObject *parent) : NiriRecordModel(parent)
{
    auto *session = NiriSession::instance();
    if (!session)
        return;
    m_rows = session->state().windowIds();
    m_revision = session->state().revision();
    connect(session, &NiriSession::stateProject, this, [this](const auto &c) {
        reconcile(c.windows, c.revision, [](quint64 fields) {
            QList<int> roles{WindowRole};
            using S = NiriEventStreamState;
            for (const auto &pair :
                 {qMakePair(S::Title, TitleRole), qMakePair(S::AppId, AppIdRole),
                  qMakePair(S::WindowPid, PidRole), qMakePair(S::WorkspaceId, WorkspaceIdRole),
                  qMakePair(S::WindowFocused, IsFocusedRole),
                  qMakePair(S::Floating, IsFloatingRole), qMakePair(S::WindowUrgent, IsUrgentRole),
                  qMakePair(S::Layout, LayoutRole),
                  qMakePair(S::FocusTimestamp, FocusTimestampRole)}) {
                if (fields & pair.first)
                    roles.append(pair.second);
            }
            return roles;
        });
    });
    session->ensureStarted();
}

int NiriWindowsModel::rowCount(const QModelIndex &parent) const
{
    return NiriSession::checkThread(this) && !parent.isValid() ? m_rows.size() : 0;
}

QVariant NiriWindowsModel::data(const QModelIndex &index, int role) const
{
    if (!NiriSession::checkThread(this) || !index.isValid() || index.model() != this
        || index.column() != 0 || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const auto entry = rowEntry(
            index.row(), [](quint64 id) { return NiriSession::instance()->state().window(id); });
    if (!entry)
        return {};
    const auto &w = *entry;
    switch (role) {
    case WindowRole:
        return QVariant::fromValue(w);
    case IdRole:
        return w.qmlId();
    case TitleRole:
        return w.qmlTitle();
    case AppIdRole:
        return w.qmlAppId();
    case PidRole:
        return w.qmlPid();
    case WorkspaceIdRole:
        return w.qmlWorkspaceId();
    case IsFocusedRole:
        return w.isFocused;
    case IsFloatingRole:
        return w.isFloating;
    case IsUrgentRole:
        return w.isUrgent;
    case LayoutRole:
        return QVariant::fromValue(w.layout);
    case FocusTimestampRole:
        return w.qmlFocusTimestamp();
    default:
        return {};
    }
}

QHash<int, QByteArray> NiriWindowsModel::roleNames() const
{
    return {{WindowRole, "window"},
            {IdRole, "id"},
            {TitleRole, "title"},
            {AppIdRole, "appId"},
            {PidRole, "pid"},
            {WorkspaceIdRole, "workspaceId"},
            {IsFocusedRole, "isFocused"},
            {IsFloatingRole, "isFloating"},
            {IsUrgentRole, "isUrgent"},
            {LayoutRole, "layout"},
            {FocusTimestampRole, "focusTimestamp"}};
}
