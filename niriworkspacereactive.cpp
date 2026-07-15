#include "niriworkspacereactive.h"

NiriWorkspaceReactive::NiriWorkspaceReactive(QObject *parent)
    : QObject(parent)
{
}

QString NiriWorkspaceReactive::workspaceName() const { return m_workspaceName; }
void NiriWorkspaceReactive::setWorkspaceName(const QString &name) { m_workspaceName = name; emit workspaceNameChanged(); }
quint64 NiriWorkspaceReactive::id() const { return m_id; }
int NiriWorkspaceReactive::idx() const { return m_idx; }
QString NiriWorkspaceReactive::name() const { return m_name; }
bool NiriWorkspaceReactive::isActive() const { return m_isActive; }
