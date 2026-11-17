#include "niriwindowreactive.h"

#include "nirievents.h"
#include "nirisession.h"
#include "generated/niriipccodec_generated.h"

NiriWindowReactive::NiriWindowReactive(QObject *parent) : QObject(parent)
{
    NiriEvents *events = NiriEvents::instance();
    if (!events)
        return;
    connect(events, &NiriEvents::windowOpenedOrChanged, this,
            &NiriWindowReactive::onWindowOpenedOrChanged);
    connect(events->nativeEvents(), &NiriNativeEvents::windowClosed, this,
            &NiriWindowReactive::onWindowClosed);
    connect(events, &NiriEvents::windowsChanged, this, &NiriWindowReactive::onWindowsChanged);
    connect(events->nativeEvents(), &NiriNativeEvents::windowFocusChanged, this,
            &NiriWindowReactive::onWindowFocusChanged);
    NiriSession::instance()->ensureStarted();
}

std::optional<quint64> NiriWindowReactive::windowId() const
{
    return NiriSession::checkThread(this) ? m_windowId : std::nullopt;
}

void NiriWindowReactive::setQmlWindowId(const QVariant &id)
{
    std::optional<quint64> value;
    NiriIpc::DecodeError error;
    if (NiriIpc::idInput(id, value, error))
        setWindowId(value);
}

void NiriWindowReactive::setWindowId(std::optional<quint64> id)
{
    if (!NiriSession::checkThread(this))
        return;
    if (m_windowId == id)
        return;
    m_windowId = id;
    emit windowIdChanged();
    // Clear current data; consumers will get it via next event or windowsChanged snapshot
    if (m_valid) {
        clear();
        emit windowChanged();
    }
    // Bootstrap from cached snapshot if available
    QVariantList snapshot = NiriEvents::instance()->lastWindowsSnapshot();
    for (const QVariant &v : snapshot) {
        NiriWindow w = v.value<NiriWindow>();
        if (w.id == id) {
            updateFrom(w);
            break;
        }
    }
}

bool NiriWindowReactive::valid() const
{
    return NiriSession::checkThread(this) && m_valid;
}
quint64 NiriWindowReactive::id() const
{
    return NiriSession::checkThread(this) ? m_data.id : 0;
}
std::optional<QString> NiriWindowReactive::title() const
{
    return NiriSession::checkThread(this) ? m_data.title : std::nullopt;
}
std::optional<QString> NiriWindowReactive::appId() const
{
    return NiriSession::checkThread(this) ? m_data.appId : std::nullopt;
}
std::optional<qint32> NiriWindowReactive::pid() const
{
    return NiriSession::checkThread(this) ? m_data.pid : std::nullopt;
}
std::optional<quint64> NiriWindowReactive::workspaceId() const
{
    return NiriSession::checkThread(this) ? m_data.workspaceId : std::nullopt;
}
bool NiriWindowReactive::isFocused() const
{
    return NiriSession::checkThread(this) && m_data.isFocused;
}
bool NiriWindowReactive::isFloating() const
{
    return NiriSession::checkThread(this) && m_data.isFloating;
}
bool NiriWindowReactive::isUrgent() const
{
    return NiriSession::checkThread(this) && m_data.isUrgent;
}
NiriWindowLayout NiriWindowReactive::layout() const
{
    return NiriSession::checkThread(this) ? m_data.layout : NiriWindowLayout{};
}
std::optional<NiriTimestamp> NiriWindowReactive::focusTimestamp() const
{
    return NiriSession::checkThread(this) ? m_data.focusTimestamp : std::nullopt;
}

void NiriWindowReactive::updateFrom(const NiriWindow &w)
{
    m_data = w;
    m_valid = true;
    emit windowChanged();
}

void NiriWindowReactive::clear()
{
    m_data = NiriWindow{};
    m_valid = false;
}

void NiriWindowReactive::onWindowOpenedOrChanged(const NiriWindow &window)
{
    if (window.id != m_windowId)
        return;
    updateFrom(window);
}

void NiriWindowReactive::onWindowClosed(quint64 id)
{
    if (id != m_windowId)
        return;
    clear();
    emit windowChanged();
}

void NiriWindowReactive::onWindowsChanged(const QVariantList &windows)
{
    if (!m_windowId)
        return;
    for (const QVariant &v : windows) {
        NiriWindow w = v.value<NiriWindow>();
        if (w.id == m_windowId) {
            updateFrom(w);
            return;
        }
    }
    // Not found in snapshot
    if (m_valid) {
        clear();
        emit windowChanged();
    }
}

void NiriWindowReactive::onWindowFocusChanged(std::optional<quint64> id)
{
    if (!m_windowId || !m_valid)
        return;
    bool wasFocused = m_data.isFocused;
    bool nowFocused = id && id == m_windowId;
    if (wasFocused != nowFocused) {
        m_data.isFocused = nowFocused;
        emit windowChanged();
    }
}
