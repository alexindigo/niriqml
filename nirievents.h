#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

#include "niritypes.h"

// Native scalar channel. QML observes only the string/null projection below.
// E6b generates the complete native event surface; this carries existing deltas.
class NiriNativeEvents : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void workspaceActivated(quint64 workspaceId, bool focused);
    void workspaceActiveWindowChanged(quint64 workspaceId, std::optional<quint64> windowId);
    void windowClosed(quint64 windowId);
    void windowFocusChanged(std::optional<quint64> windowId);
    void windowFocusTimestampChanged(quint64 windowId, std::optional<NiriTimestamp> timestamp);
};

class NiriEvents : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(NiriEvents)

public:
    static NiriEvents *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriEvents *instance();

    void dispatchEvent(const QString &name, const QVariantMap &payload);
    void dispatchEvent(const QString &name, const QVariantMap &payload, quint64 producingEpoch);
    void dispatchWireEvent(const QString &name, const NiriWire::WireValue &payload,
                           quint64 producingEpoch);
    NiriNativeEvents *nativeEvents() { return m_source ? m_source->nativeEvents() : &m_native; }

    // Snapshot getters: last-known state from initial event burst.
    // Consumers instantiated AFTER events fired can bootstrap from these.
    QVariantList lastWindowsSnapshot() const;
    QVariantList lastWorkspacesSnapshot() const;
    NiriKeyboardLayouts lastKeyboardLayouts() const;
    bool lastOverviewOpen() const;
    std::optional<quint64> lastFocusedWindowId() const;

signals:
    void workspacesChanged(const QVariantList &workspaces);
    // Emitted when a workspace is activated on its output. `focused` indicates
    // whether keyboard focus followed to the activated workspace.
    void workspaceActivated(const QString &workspaceId, bool focused);
    void workspaceActiveWindowChanged(const QString &workspaceId, const QVariant &windowId);
    void windowsChanged(const QVariantList &windows);
    void windowOpenedOrChanged(const NiriWindow &window);
    void windowClosed(const QString &windowId);
    void windowFocusChanged(const QVariant &windowId);
    void windowLayoutsChanged(const QVariantList &changes);
    void keyboardLayoutsChanged(const NiriKeyboardLayouts &layouts);
    void keyboardLayoutSwitched(int idx);
    void overviewOpenedOrClosed(bool isOpen);
    void configLoaded(bool failed);
    void castsChanged(const QVariantList &casts);
    void castStartedOrChanged(const NiriCast &cast);
    void windowFocusTimestampChanged(const QString &windowId, const QVariant &focusTimestamp);
    void rawEvent(const QString &name, const QVariantMap &payload);
    void decodeError(const QString &path, const QString &message, const QVariantMap &payload);

private:
    explicit NiriEvents(QObject *parent = nullptr, NiriEvents *source = nullptr);
    NiriEvents *m_source = nullptr;
    NiriNativeEvents m_native;

    QVariantList m_lastWindows;
    QVariantList m_lastWorkspaces;
    NiriKeyboardLayouts m_lastKeyboardLayouts;
    bool m_lastOverviewOpen = false;
    std::optional<quint64> m_lastFocusedWindowId;
};
