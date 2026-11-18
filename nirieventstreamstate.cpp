#include "nirieventstreamstate.h"
#include <algorithm>

namespace {
using State = NiriEventStreamState;
quint64 fields(const NiriWindow &a, const NiriWindow &b)
{
    quint64 mask = 0;
#define FIELD(member, bit)    \
    if (a.member != b.member) \
    mask |= State::bit
    FIELD(appId, AppId);
    FIELD(focusTimestamp, FocusTimestamp);
    FIELD(isFloating, Floating);
    FIELD(isFocused, WindowFocused);
    FIELD(isUrgent, WindowUrgent);
    FIELD(layout, Layout);
    FIELD(pid, WindowPid);
    FIELD(title, Title);
    FIELD(workspaceId, WorkspaceId);
    FIELD(extensions, WindowExtensions);
#undef FIELD
    return mask;
}
quint64 fields(const NiriWorkspace &a, const NiriWorkspace &b)
{
    quint64 mask = 0;
#define FIELD(member, bit)    \
    if (a.member != b.member) \
    mask |= State::bit
    FIELD(activeWindowId, ActiveWindowId);
    FIELD(idx, Index);
    FIELD(isActive, Active);
    FIELD(isFocused, WorkspaceFocused);
    FIELD(isUrgent, WorkspaceUrgent);
    FIELD(name, Name);
    FIELD(output, Output);
    FIELD(extensions, WorkspaceExtensions);
#undef FIELD
    return mask;
}
quint64 fields(const NiriCast &a, const NiriCast &b)
{
    quint64 mask = 0;
#define FIELD(member, bit)    \
    if (a.member != b.member) \
    mask |= State::bit
    FIELD(isActive, CastActive);
    FIELD(isDynamicTarget, DynamicTarget);
    FIELD(kind, Kind);
    FIELD(pid, CastPid);
    FIELD(pwNodeId, NodeId);
    FIELD(sessionId, SessionId);
    FIELD(target, Target);
    FIELD(extensions, CastExtensions);
#undef FIELD
    return mask;
}
template <typename T>
constexpr quint64 allFields();
template <>
constexpr quint64 allFields<NiriWindow>()
{
    return State::AllWindowFields;
}
template <>
constexpr quint64 allFields<NiriWorkspace>()
{
    return State::AllWorkspaceFields;
}
template <>
constexpr quint64 allFields<NiriCast>()
{
    return State::AllCastFields;
}

template <typename T>
void put(QMap<quint64, State::Entry<T>> &map, QMap<quint64, State::RecordChange<T>> &changes,
         quint64 id, State::Entry<T> after)
{
    const auto before = map.value(id);
    if ((!before && !after) || (before && after && *before == *after))
        return;
    auto original = changes.contains(id) ? changes.value(id).before : before;
    if ((!original && !after) || (original && after && *original == *after))
        changes.remove(id);
    else
        changes.insert(
                id,
                {original, after, original && after ? fields(*original, *after) : allFields<T>()});
    if (after)
        map.insert(id, std::move(after));
    else
        map.remove(id);
}
template <typename T>
void replace(QMap<quint64, State::Entry<T>> &map, QMap<quint64, State::RecordChange<T>> &changes,
             const QList<T> &values, quint64 T::*key)
{
    QMap<quint64, State::Entry<T>> next;
    for (const auto &value : values)
        next.insert(value.*key, std::make_shared<const T>(value));
    const auto ids = map.keys();
    for (auto id : ids) {
        if (!next.contains(id))
            put(map, changes, id, State::Entry<T>{});
    }
    for (auto it = next.cbegin(); it != next.cend(); ++it)
        put(map, changes, it.key(), it.value());
}
template <typename T>
QList<T> ordered(const QMap<quint64, State::Entry<T>> &map)
{
    QList<T> result;
    result.reserve(map.size());
    for (const auto &entry : map)
        result.append(*entry);
    return result;
}
} // namespace

bool State::Changes::changed() const
{
    return !windows.isEmpty() || !workspaces.isEmpty() || !casts.isEmpty() || keyboardLayouts
            || overview || config || readiness || touchpad;
}

State::ApplyResult State::apply(const NiriEvent &event, quint64 token)
{
    ApplyResult result;
    result.changes.epoch = m_epoch;
    result.changes.revision = m_revision;
    if (token != m_epoch) {
        result.status = ApplyResult::StaleEpoch;
        return result;
    }
    auto &c = result.changes;
    c.focusedWindowBefore = focusedWindowId();
    c.focusedWorkspaceBefore = focusedWorkspaceId();
    auto reject = [&](const QString &message) {
        result.status = ApplyResult::ProtocolError;
        result.error = message;
    };
#define PAYLOAD(Tag) const auto &p = std::get<NiriEvent::Tag##Value>(event.value).payload
    // Validate all targets before any mutation, unlike upstream's panic path
    // (state.rs:227-233). Cross-part dangling IDs are deliberately not checked.
    switch (event.kind()) {
    case NiriEvent::WorkspaceActivated: {
        PAYLOAD(WorkspaceActivated);
        if (!m_workspaces.contains(p.id))
            reject(QStringLiteral("Activated workspace is missing"));
        break;
    }
    case NiriEvent::WorkspaceActiveWindowChanged: {
        PAYLOAD(WorkspaceActiveWindowChanged);
        if (!m_workspaces.contains(p.workspaceId))
            reject(QStringLiteral("Changed workspace is missing"));
        break;
    }
    case NiriEvent::WindowClosed: {
        PAYLOAD(WindowClosed);
        if (!m_windows.contains(p.id))
            reject(QStringLiteral("Closed window is missing"));
        break;
    }
    case NiriEvent::WindowLayoutsChanged: {
        PAYLOAD(WindowLayoutsChanged);
        for (const auto &change : p.changes) {
            if (!m_windows.contains(std::get<0>(change))) {
                reject(QStringLiteral("Layout window is missing"));
                break;
            }
        }
        break;
    }
    case NiriEvent::KeyboardLayoutSwitched:
        if (!m_layouts)
            reject(QStringLiteral("Keyboard layout switch precedes layouts"));
        break;
    case NiriEvent::CastStopped: {
        PAYLOAD(CastStopped);
        if (!m_casts.contains(p.streamId))
            reject(QStringLiteral("Stopped cast is missing"));
        break;
    }
    default:
        break;
    }
    if (result.status != ApplyResult::Applied)
        return result;

    switch (event.kind()) {
    case NiriEvent::WorkspacesChanged: {
        PAYLOAD(WorkspacesChanged);
        replace(m_workspaces, c.workspaces, p.workspaces, &NiriWorkspace::id);
        break;
    }
    case NiriEvent::WorkspaceUrgencyChanged: {
        PAYLOAD(WorkspaceUrgencyChanged);
        if (auto old = workspace(p.id)) {
            auto ws = *old;
            ws.isUrgent = p.urgent;
            put(m_workspaces, c.workspaces, p.id, std::make_shared<const NiriWorkspace>(ws));
        }
        break;
    }
    case NiriEvent::WorkspaceActivated: {
        PAYLOAD(WorkspaceActivated);
        const auto output = workspace(p.id)->output;
        for (auto id : m_workspaces.keys()) {
            auto ws = *workspace(id);
            if (ws.output == output)
                ws.isActive = id == p.id;
            if (p.focused)
                ws.isFocused = id == p.id;
            put(m_workspaces, c.workspaces, id, std::make_shared<const NiriWorkspace>(ws));
        }
        break;
    }
    case NiriEvent::WorkspaceActiveWindowChanged: {
        PAYLOAD(WorkspaceActiveWindowChanged);
        auto ws = *workspace(p.workspaceId);
        ws.activeWindowId = p.activeWindowId;
        put(m_workspaces, c.workspaces, ws.id, std::make_shared<const NiriWorkspace>(ws));
        break;
    }
    case NiriEvent::WindowsChanged: {
        PAYLOAD(WindowsChanged);
        replace(m_windows, c.windows, p.windows, &NiriWindow::id);
        break;
    }
    case NiriEvent::WindowOpenedOrChanged: {
        PAYLOAD(WindowOpenedOrChanged);
        put(m_windows, c.windows, p.window.id, std::make_shared<const NiriWindow>(p.window));
        if (p.window.isFocused) {
            for (auto id : m_windows.keys()) {
                if (id != p.window.id && window(id)->isFocused) {
                    auto win = *window(id);
                    win.isFocused = false;
                    put(m_windows, c.windows, id, std::make_shared<const NiriWindow>(win));
                }
            }
        }
        break;
    }
    case NiriEvent::WindowClosed: {
        PAYLOAD(WindowClosed);
        put(m_windows, c.windows, p.id, Entry<NiriWindow>{});
        break;
    }
    case NiriEvent::WindowFocusChanged: {
        PAYLOAD(WindowFocusChanged);
        for (auto id : m_windows.keys()) {
            auto win = *window(id);
            win.isFocused = p.id == id;
            put(m_windows, c.windows, id, std::make_shared<const NiriWindow>(win));
        }
        break;
    }
    case NiriEvent::WindowFocusTimestampChanged: {
        PAYLOAD(WindowFocusTimestampChanged);
        if (auto old = window(p.id)) {
            auto win = *old;
            win.focusTimestamp = p.focusTimestamp;
            put(m_windows, c.windows, p.id, std::make_shared<const NiriWindow>(win));
        }
        break;
    }
    case NiriEvent::WindowUrgencyChanged: {
        PAYLOAD(WindowUrgencyChanged);
        if (auto old = window(p.id)) {
            auto win = *old;
            win.isUrgent = p.urgent;
            put(m_windows, c.windows, p.id, std::make_shared<const NiriWindow>(win));
        }
        break;
    }
    case NiriEvent::WindowLayoutsChanged: {
        PAYLOAD(WindowLayoutsChanged);
        for (const auto &change : p.changes) {
            auto win = *window(std::get<0>(change));
            win.layout = std::get<1>(change);
            put(m_windows, c.windows, win.id, std::make_shared<const NiriWindow>(win));
        }
        break;
    }
    case NiriEvent::KeyboardLayoutsChanged: {
        PAYLOAD(KeyboardLayoutsChanged);
        c.keyboardLayouts = m_layouts != p.keyboardLayouts;
        m_layouts = p.keyboardLayouts;
        break;
    }
    case NiriEvent::KeyboardLayoutSwitched: {
        PAYLOAD(KeyboardLayoutSwitched);
        c.keyboardLayouts = m_layouts->currentIdx != p.idx;
        m_layouts->currentIdx = p.idx;
        break;
    }
    case NiriEvent::OverviewOpenedOrClosed: {
        PAYLOAD(OverviewOpenedOrClosed);
        c.overview = m_overview != p.isOpen;
        m_overview = p.isOpen;
        break;
    }
    case NiriEvent::ConfigLoaded: {
        PAYLOAD(ConfigLoaded);
        c.config = m_config != p.failed;
        m_config = p.failed;
        break;
    }
    case NiriEvent::CastsChanged: {
        PAYLOAD(CastsChanged);
        replace(m_casts, c.casts, p.casts, &NiriCast::streamId);
        c.readiness = !m_ready;
        m_ready = true;
        break;
    }
    case NiriEvent::CastStartedOrChanged: {
        PAYLOAD(CastStartedOrChanged);
        put(m_casts, c.casts, p.cast.streamId, std::make_shared<const NiriCast>(p.cast));
        break;
    }
    case NiriEvent::CastStopped: {
        PAYLOAD(CastStopped);
        put(m_casts, c.casts, p.streamId, Entry<NiriCast>{});
        break;
    }
    case NiriEvent::TouchpadToggled: {
        PAYLOAD(TouchpadToggled);
        c.touchpad = m_touchpad != p.on;
        m_touchpad = p.on;
        break;
    }
    case NiriEvent::ScreenshotCaptured:
    case NiriEvent::Unknown:
    case NiriEvent::Invalid:
        break;
    }
#undef PAYLOAD
    updateIndexes(c);
    c.revision = ++m_revision;
    return result;
}

State::Changes State::reset(quint64 token, const QString &reason)
{
    Changes c;
    c.epoch = token;
    c.revision = ++m_revision;
    c.reset = true;
    c.reason = reason;
    c.focusedWindowBefore = focusedWindowId();
    c.focusedWorkspaceBefore = focusedWorkspaceId();
    replace(m_windows, c.windows, {}, &NiriWindow::id);
    replace(m_workspaces, c.workspaces, {}, &NiriWorkspace::id);
    replace(m_casts, c.casts, {}, &NiriCast::streamId);
    c.keyboardLayouts = m_layouts.has_value();
    c.overview = m_overview;
    c.config = m_config;
    c.readiness = m_ready;
    c.touchpad = m_touchpad.has_value();
    m_layouts.reset();
    m_touchpad.reset();
    m_overview = m_config = m_ready = false;
    m_epoch = token;
    updateIndexes(c);
    return c;
}

QList<NiriEvent> State::replicate() const
{
    // v26.04 state.rs:97-105: casts ends the released six-part burst.
    // Pinned main appends touchpad after casts (state.rs:120). Do not invent
    // a touchpad value when this optional extension has not been received.
    QList<NiriEvent> result{NiriEvent::fromWorkspacesChanged({workspacesSnapshot(), {}}),
                            NiriEvent::fromWindowsChanged({windowsSnapshot(), {}})};
    if (m_layouts)
        result.append(NiriEvent::fromKeyboardLayoutsChanged({*m_layouts, {}}));
    result.append(NiriEvent::fromOverviewOpenedOrClosed({m_overview, {}}));
    result.append(NiriEvent::fromConfigLoaded({m_config, {}}));
    result.append(NiriEvent::fromCastsChanged({castsSnapshot(), {}}));
    if (m_touchpad)
        result.append(NiriEvent::fromTouchpadToggled({*m_touchpad, {}}));
    return result;
}

QList<NiriWindow> State::windowsSnapshot() const
{
    return ordered(m_windows);
}
QList<NiriWorkspace> State::workspacesSnapshot() const
{
    return ordered(m_workspaces);
}
QList<NiriCast> State::castsSnapshot() const
{
    return ordered(m_casts);
}
std::optional<quint64> State::focusedWindowId() const
{
    return m_focusedWindows.isEmpty()
            ? std::nullopt
            : std::optional<quint64>(
                      *std::min_element(m_focusedWindows.cbegin(), m_focusedWindows.cend()));
}
std::optional<quint64> State::focusedWorkspaceId() const
{
    return m_focusedWorkspaces.isEmpty()
            ? std::nullopt
            : std::optional<quint64>(
                      *std::min_element(m_focusedWorkspaces.cbegin(), m_focusedWorkspaces.cend()));
}

State::Entry<NiriWorkspace> State::workspaceByName(const QString &name, const Changes *before) const
{
    auto ids = m_workspaceNames.value(name);
    if (before) {
        for (auto it = before->workspaces.cbegin(); it != before->workspaces.cend(); ++it) {
            ids.remove(it.key());
            if (it->before && it->before->name == name)
                ids.insert(it.key());
        }
    }
    if (ids.isEmpty())
        return {};
    const auto id = *std::min_element(ids.cbegin(), ids.cend());
    if (before && before->workspaces.contains(id))
        return before->workspaces.value(id).before;
    return workspace(id);
}

void State::updateIndexes(const Changes &c)
{
    for (auto it = c.windows.cbegin(); it != c.windows.cend(); ++it) {
        m_focusedWindows.remove(it.key());
        if (it->after && it->after->isFocused)
            m_focusedWindows.insert(it.key());
    }
    for (auto it = c.workspaces.cbegin(); it != c.workspaces.cend(); ++it) {
        m_focusedWorkspaces.remove(it.key());
        if (it->before && it->before->name) {
            auto name = m_workspaceNames.find(*it->before->name);
            if (name != m_workspaceNames.end()) {
                name->remove(it.key());
                if (name->isEmpty())
                    m_workspaceNames.erase(name);
            }
        }
        if (it->after) {
            if (it->after->isFocused)
                m_focusedWorkspaces.insert(it.key());
            if (it->after->name)
                m_workspaceNames[*it->after->name].insert(it.key());
        }
    }
}
