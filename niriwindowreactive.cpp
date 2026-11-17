#include "niriwindowreactive.h"
#include "niriselectors.h"
#include "nirisession.h"
#include "generated/niriipccodec_generated.h"
#include <QQmlInfo>

NiriWindowReactive::NiriWindowReactive(QObject *parent) : QObject(parent)
{
    auto *session = NiriSession::instance();
    if (!session)
        return;
    m_revision = session->state().revision();
    connect(session, &NiriSession::stateUpdated, this, [this, session](const auto &c) {
        if (c.revision <= m_revision)
            return;
        m_revision = c.revision;
        if (m_windowId && c.windows.contains(*m_windowId))
            notify(values(NiriSelectors::window(session->state(), m_windowId, &c)));
    });
    session->ensureStarted();
}

NiriWindowReactive::Entry NiriWindowReactive::selected() const
{
    return NiriSession::checkThread(this)
            ? NiriSelectors::window(NiriSession::instance()->state(), m_windowId)
            : nullptr;
}

std::optional<quint64> NiriWindowReactive::windowId() const
{
    return NiriSession::checkThread(this) ? m_windowId : std::nullopt;
}

void NiriWindowReactive::setQmlWindowId(const QVariant &id)
{
    if (!NiriSession::checkThread(this))
        return;
    std::optional<quint64> value;
    NiriIpc::DecodeError error;
    if (NiriIpc::idInput(id, value, error))
        setWindowId(value);
    else
        qmlWarning(this) << error.path << error.message;
}

void NiriWindowReactive::setWindowId(std::optional<quint64> id)
{
    if (!NiriSession::checkThread(this) || m_windowId == id)
        return;
    auto before = values(selected());
    m_windowId = id;
    notify(std::move(before));
}

NiriProjectionNotifications::Values NiriWindowReactive::values(const Entry &entry) const
{
    const auto w = entry ? *entry : NiriWindow{};
    return {qmlWindowId(),
            bool(entry),
            entry ? w.qmlId() : QVariant::fromValue(nullptr),
            w.qmlTitle(),
            w.qmlAppId(),
            w.qmlPid(),
            w.qmlWorkspaceId(),
            w.isFocused,
            w.isFloating,
            w.isUrgent,
            QVariant::fromValue(w.layout),
            w.qmlFocusTimestamp(),
            entry ? QVariant::fromValue(w) : QVariant::fromValue(nullptr)};
}

void NiriWindowReactive::notify(NiriProjectionNotifications::Values before)
{
    m_notifications.flush(
            this, std::move(before), [this] { return values(selected()); },
            {[this] { emit windowIdChanged(); }, [this] { emit validChanged(); },
             [this] { emit idChanged(); }, [this] { emit titleChanged(); },
             [this] { emit appIdChanged(); }, [this] { emit pidChanged(); },
             [this] { emit workspaceIdChanged(); }, [this] { emit isFocusedChanged(); },
             [this] { emit isFloatingChanged(); }, [this] { emit isUrgentChanged(); },
             [this] { emit layoutChanged(); }, [this] { emit focusTimestampChanged(); },
             [this] { emit windowChanged(); }});
}

bool NiriWindowReactive::valid() const
{
    return bool(selected());
}
quint64 NiriWindowReactive::id() const
{
    auto w = selected();
    return w ? w->id : 0;
}
std::optional<QString> NiriWindowReactive::title() const
{
    auto w = selected();
    return w ? w->title : std::nullopt;
}
std::optional<QString> NiriWindowReactive::appId() const
{
    auto w = selected();
    return w ? w->appId : std::nullopt;
}
std::optional<qint32> NiriWindowReactive::pid() const
{
    auto w = selected();
    return w ? w->pid : std::nullopt;
}
std::optional<quint64> NiriWindowReactive::workspaceId() const
{
    auto w = selected();
    return w ? w->workspaceId : std::nullopt;
}
bool NiriWindowReactive::isFocused() const
{
    auto w = selected();
    return w && w->isFocused;
}
bool NiriWindowReactive::isFloating() const
{
    auto w = selected();
    return w && w->isFloating;
}
bool NiriWindowReactive::isUrgent() const
{
    auto w = selected();
    return w && w->isUrgent;
}
NiriWindowLayout NiriWindowReactive::layout() const
{
    auto w = selected();
    return w ? w->layout : NiriWindowLayout{};
}
std::optional<NiriTimestamp> NiriWindowReactive::focusTimestamp() const
{
    auto w = selected();
    return w ? w->focusTimestamp : std::nullopt;
}
std::optional<NiriWindow> NiriWindowReactive::window() const
{
    auto w = selected();
    return w ? std::optional<NiriWindow>(*w) : std::nullopt;
}
