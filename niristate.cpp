#include "niristate.h"

#include <QQmlEngine>

NiriState *NiriState::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

NiriState *NiriState::instance()
{
    static NiriState s;
    return &s;
}

NiriState::NiriState(QObject *parent)
    : QObject(parent)
{
}

quint64 NiriState::focusedWindowId() const { return m_focusedWindowId; }
QString NiriState::focusedWindowAppId() const { return m_focusedWindowAppId; }
quint64 NiriState::focusedWorkspaceId() const { return m_focusedWorkspaceId; }
QString NiriState::activeOutput() const { return m_activeOutput; }
int NiriState::windowCount() const { return m_windowCount; }
int NiriState::workspaceCount() const { return m_workspaceCount; }
