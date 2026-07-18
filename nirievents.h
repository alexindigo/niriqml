#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

#include "niritypes.h"

class NiriEvents : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(NiriEvents)

public:
    static NiriEvents *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriEvents *instance();

    void dispatchEvent(const QString &name, const QVariantMap &payload);

    // Snapshot getters: last-known state from initial event burst.
    // Consumers instantiated AFTER events fired can bootstrap from these.
    QVariantList lastWindowsSnapshot() const;
    QVariantList lastWorkspacesSnapshot() const;
    NiriKeyboardLayouts lastKeyboardLayouts() const;
    bool lastOverviewOpen() const;
    quint64 lastFocusedWindowId() const;

signals:
    void workspacesChanged(const QVariantList &workspaces);
    // Emitted when a workspace is activated on its output. `focused` indicates
    // whether keyboard focus followed to the activated workspace.
    void workspaceActivated(quint64 workspaceId, bool focused);
    void workspaceActiveWindowChanged(quint64 workspaceId, quint64 windowId);
    void windowsChanged(const QVariantList &windows);
    void windowOpenedOrChanged(const NiriWindow &window);
    void windowClosed(quint64 windowId);
    void windowFocusChanged(quint64 windowId);
    void windowLayoutsChanged(const QVariantList &changes);
    void keyboardLayoutsChanged(const NiriKeyboardLayouts &layouts);
    void keyboardLayoutSwitched(int idx);
    void overviewOpenedOrClosed(bool isOpen);
    void configLoaded(bool failed);
    void castsChanged(const QVariantList &casts);
    void windowFocusTimestampChanged(quint64 windowId, const NiriTimestamp &focusTimestamp);
    void rawEvent(const QString &name, const QVariantMap &payload);

private:
    explicit NiriEvents(QObject *parent = nullptr);

    QVariantList m_lastWindows;
    QVariantList m_lastWorkspaces;
    NiriKeyboardLayouts m_lastKeyboardLayouts;
    bool m_lastOverviewOpen = false;
    quint64 m_lastFocusedWindowId = 0;
};
