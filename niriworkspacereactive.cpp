#include "niriworkspacereactive.h"

#include "nirievents.h"

NiriWorkspaceReactive::NiriWorkspaceReactive(QObject *parent) : QObject(parent)
{
    NiriEvents *events = NiriEvents::instance();
    connect(events, &NiriEvents::workspacesChanged, this,
            &NiriWorkspaceReactive::onWorkspacesChanged);
    connect(events, &NiriEvents::workspaceActivated, this,
            &NiriWorkspaceReactive::onWorkspaceActivated);
    connect(events, &NiriEvents::workspaceActiveWindowChanged, this,
            &NiriWorkspaceReactive::onWorkspaceActiveWindowChanged);
}

QString NiriWorkspaceReactive::workspaceName() const
{
    return m_selectorName;
}
void NiriWorkspaceReactive::setWorkspaceName(const QString &name)
{
    if (m_selectorName == name)
        return;
    m_selectorName = name;
    emit workspaceNameChanged();
    if (m_valid) {
        clear();
        emit workspaceChanged();
    }
    // Bootstrap
    QVariantList snapshot = NiriEvents::instance()->lastWorkspacesSnapshot();
    for (const QVariant &v : snapshot) {
        NiriWorkspace w = v.value<NiriWorkspace>();
        if (w.name == name && !name.isEmpty()) {
            updateFrom(w);
            break;
        }
    }
}

quint64 NiriWorkspaceReactive::workspaceId() const
{
    return m_selectorId;
}
void NiriWorkspaceReactive::setWorkspaceId(quint64 id)
{
    if (m_selectorId == id)
        return;
    m_selectorId = id;
    emit workspaceIdChanged();
    if (m_valid) {
        clear();
        emit workspaceChanged();
    }
    // Bootstrap
    QVariantList snapshot = NiriEvents::instance()->lastWorkspacesSnapshot();
    for (const QVariant &v : snapshot) {
        NiriWorkspace w = v.value<NiriWorkspace>();
        if (w.id == id && id != 0) {
            updateFrom(w);
            break;
        }
    }
}

bool NiriWorkspaceReactive::valid() const
{
    return m_valid;
}
quint64 NiriWorkspaceReactive::id() const
{
    return m_data.id;
}
int NiriWorkspaceReactive::idx() const
{
    return m_data.idx;
}
QString NiriWorkspaceReactive::name() const
{
    return m_data.name;
}
QString NiriWorkspaceReactive::output() const
{
    return m_data.output;
}
bool NiriWorkspaceReactive::isUrgent() const
{
    return m_data.isUrgent;
}
bool NiriWorkspaceReactive::isActive() const
{
    return m_data.isActive;
}
bool NiriWorkspaceReactive::isFocused() const
{
    return m_data.isFocused;
}
quint64 NiriWorkspaceReactive::activeWindowId() const
{
    return m_data.activeWindowId;
}

bool NiriWorkspaceReactive::matches(const NiriWorkspace &w) const
{
    if (!m_selectorName.isEmpty())
        return w.name == m_selectorName;
    if (m_selectorId != 0)
        return w.id == m_selectorId;
    return false;
}

void NiriWorkspaceReactive::updateFrom(const NiriWorkspace &w)
{
    m_data = w;
    m_valid = true;
    emit workspaceChanged();
}

void NiriWorkspaceReactive::clear()
{
    m_data = NiriWorkspace{ };
    m_valid = false;
}

void NiriWorkspaceReactive::onWorkspacesChanged(const QVariantList &workspaces)
{
    for (const QVariant &v : workspaces) {
        NiriWorkspace w = v.value<NiriWorkspace>();
        if (matches(w)) {
            updateFrom(w);
            return;
        }
    }
    if (m_valid) {
        clear();
        emit workspaceChanged();
    }
}

void NiriWorkspaceReactive::onWorkspaceActivated(quint64 workspaceId, bool focused)
{
    if (!m_valid)
        return;
    // Look up the activated workspace to find its output. If it matches our
    // workspace's output, our isActive flag needs updating.
    const NiriWorkspace *target = nullptr;
    QVariantList snapshot = NiriEvents::instance()->lastWorkspacesSnapshot();
    for (const QVariant &v : snapshot) {
        NiriWorkspace w = v.value<NiriWorkspace>();
        if (w.id == workspaceId) {
            target = &w;
            break;
        }
    }
    if (!target)
        return;

    if (m_data.output != target->output && m_data.id != workspaceId)
        return;

    bool changed = false;
    bool nowActive = (m_data.id == workspaceId);
    if (m_data.isActive != nowActive) {
        m_data.isActive = nowActive;
        changed = true;
    }
    if (focused) {
        bool nowFocused = (m_data.id == workspaceId);
        if (m_data.isFocused != nowFocused) {
            m_data.isFocused = nowFocused;
            changed = true;
        }
    }
    if (changed)
        emit workspaceChanged();
}

void NiriWorkspaceReactive::onWorkspaceActiveWindowChanged(quint64 workspaceId, quint64 windowId)
{
    if (!m_valid || m_data.id != workspaceId)
        return;
    if (m_data.activeWindowId == windowId)
        return;
    m_data.activeWindowId = windowId;
    emit workspaceChanged();
}
