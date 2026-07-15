#include "niriwindowreactive.h"

NiriWindowReactive::NiriWindowReactive(QObject *parent)
    : QObject(parent)
{
}

quint64 NiriWindowReactive::windowId() const { return m_windowId; }
void NiriWindowReactive::setWindowId(quint64 id) { m_windowId = id; emit windowIdChanged(); }
quint64 NiriWindowReactive::id() const { return m_id; }
QString NiriWindowReactive::title() const { return m_title; }
QString NiriWindowReactive::appId() const { return m_appId; }
bool NiriWindowReactive::isFocused() const { return m_isFocused; }
bool NiriWindowReactive::isFloating() const { return m_isFloating; }
