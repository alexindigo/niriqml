#include "niriworkspacereactive.h"
#include "niriselectors.h"
#include "nirisession.h"
#include "generated/niriipccodec_generated.h"
#include <QQmlInfo>

NiriWorkspaceReactive::NiriWorkspaceReactive(QObject *parent) : QObject(parent)
{
    auto *session = NiriSession::instance();
    if (!session)
        return;
    m_revision = session->state().revision();
    connect(session, &NiriSession::stateUpdated, this, [this](const auto &c) {
        if (c.revision <= m_revision)
            return;
        m_revision = c.revision;
        if ((!m_selectorName.isEmpty() && !c.workspaces.isEmpty())
            || (m_selectorId && c.workspaces.contains(*m_selectorId)))
            notify();
    });
    session->ensureStarted();
}

NiriWorkspaceReactive::Entry NiriWorkspaceReactive::selected() const
{
    return NiriSession::checkThread(this)
            ? NiriSelectors::workspace(NiriSession::instance()->state(), m_selectorName,
                                       m_selectorId)
            : nullptr;
}

QString NiriWorkspaceReactive::workspaceName() const
{
    return NiriSession::checkThread(this) ? m_selectorName : QString{};
}
std::optional<quint64> NiriWorkspaceReactive::workspaceId() const
{
    return NiriSession::checkThread(this) ? m_selectorId : std::nullopt;
}
void NiriWorkspaceReactive::setWorkspaceName(const QString &name)
{
    if (!NiriSession::checkThread(this) || name == m_selectorName)
        return;
    m_selectorName = name;
    notify();
}
void NiriWorkspaceReactive::setWorkspaceId(std::optional<quint64> id)
{
    if (!NiriSession::checkThread(this) || id == m_selectorId)
        return;
    m_selectorId = id;
    notify();
}
void NiriWorkspaceReactive::setQmlWorkspaceId(const QVariant &id)
{
    if (!NiriSession::checkThread(this))
        return;
    std::optional<quint64> value;
    NiriIpc::DecodeError error;
    if (NiriIpc::idInput(id, value, error))
        setWorkspaceId(value);
    else
        qmlWarning(this) << error.path << error.message;
}

NiriProjectionNotifications::Values NiriWorkspaceReactive::values(const Selection &selection)
{
    const auto &entry = selection.record;
    const auto w = entry ? *entry : NiriWorkspace{};
    return {selection.name,
            NiriIpc::qmlValue(selection.id),
            bool(entry),
            entry ? w.qmlId() : QVariant::fromValue(nullptr),
            w.idx,
            w.qmlName(),
            w.qmlOutput(),
            w.isUrgent,
            w.isActive,
            w.isFocused,
            w.qmlActiveWindowId(),
            entry ? QVariant::fromValue(w) : QVariant::fromValue(nullptr)};
}
void NiriWorkspaceReactive::notify()
{
    m_notifications.flush(
            this, m_announced,
            [this] { return Selection{m_selectorName, m_selectorId, selected()}; }, values,
            {[this] { emit workspaceNameChanged(); }, [this] { emit workspaceIdChanged(); },
             [this] { emit validChanged(); }, [this] { emit idChanged(); },
             [this] { emit idxChanged(); }, [this] { emit nameChanged(); },
             [this] { emit outputChanged(); }, [this] { emit isUrgentChanged(); },
             [this] { emit isActiveChanged(); }, [this] { emit isFocusedChanged(); },
             [this] { emit activeWindowIdChanged(); }, [this] { emit workspaceChanged(); }});
}

bool NiriWorkspaceReactive::valid() const
{
    return bool(selected());
}
quint64 NiriWorkspaceReactive::id() const
{
    auto w = selected();
    return w ? w->id : 0;
}
int NiriWorkspaceReactive::idx() const
{
    auto w = selected();
    return w ? w->idx : 0;
}
std::optional<QString> NiriWorkspaceReactive::name() const
{
    auto w = selected();
    return w ? w->name : std::nullopt;
}
std::optional<QString> NiriWorkspaceReactive::output() const
{
    auto w = selected();
    return w ? w->output : std::nullopt;
}
bool NiriWorkspaceReactive::isUrgent() const
{
    auto w = selected();
    return w && w->isUrgent;
}
bool NiriWorkspaceReactive::isActive() const
{
    auto w = selected();
    return w && w->isActive;
}
bool NiriWorkspaceReactive::isFocused() const
{
    auto w = selected();
    return w && w->isFocused;
}
std::optional<quint64> NiriWorkspaceReactive::activeWindowId() const
{
    auto w = selected();
    return w ? w->activeWindowId : std::nullopt;
}
std::optional<NiriWorkspace> NiriWorkspaceReactive::workspace() const
{
    auto w = selected();
    return w ? std::optional<NiriWorkspace>(*w) : std::nullopt;
}
