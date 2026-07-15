#include "niriactions.h"

NiriActions *NiriActions::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

NiriActions *NiriActions::instance()
{
    static NiriActions s;
    return &s;
}

NiriActions::NiriActions(QObject *parent)
    : QObject(parent)
{
}

NiriPendingReply *NiriActions::spawn(const QStringList &command)
{
    Q_UNUSED(command)
    return new NiriPendingReply(this);
}

NiriPendingReply *NiriActions::moveWindowToWorkspace(quint64 windowId, const QVariant &reference)
{
    Q_UNUSED(windowId)
    Q_UNUSED(reference)
    return new NiriPendingReply(this);
}

NiriPendingReply *NiriActions::moveWindowToMonitor(quint64 windowId, const QString &monitor)
{
    Q_UNUSED(windowId)
    Q_UNUSED(monitor)
    return new NiriPendingReply(this);
}

NiriPendingReply *NiriActions::focusWindow(quint64 windowId)
{
    Q_UNUSED(windowId)
    return new NiriPendingReply(this);
}

NiriPendingReply *NiriActions::closeWindow(quint64 windowId)
{
    Q_UNUSED(windowId)
    return new NiriPendingReply(this);
}

NiriPendingReply *NiriActions::setWindowWidth(quint64 windowId, const QVariant &size)
{
    Q_UNUSED(windowId)
    Q_UNUSED(size)
    return new NiriPendingReply(this);
}

NiriPendingReply *NiriActions::setWindowHeight(quint64 windowId, const QVariant &size)
{
    Q_UNUSED(windowId)
    Q_UNUSED(size)
    return new NiriPendingReply(this);
}

NiriPendingReply *NiriActions::moveColumnToIndex(int index)
{
    Q_UNUSED(index)
    return new NiriPendingReply(this);
}

NiriPendingReply *NiriActions::toggleWindowFloating(quint64 windowId)
{
    Q_UNUSED(windowId)
    return new NiriPendingReply(this);
}
