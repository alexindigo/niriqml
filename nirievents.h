#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

#include "niritypes.h"
#include "generated/niriipcevents_generated.h"

class NiriEvents : public NiriIpcEvents
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
    void dispatchDocument(const NiriWire::Document &document, quint64 producingEpoch);
    NiriNativeEvents *nativeEvents()
    {
        return m_source ? m_source->nativeEvents() : NiriIpcEvents::nativeEvents();
    }

    // Snapshot getters: last-known state from initial event burst.
    // Consumers instantiated AFTER events fired can bootstrap from these.
    QVariantList lastWindowsSnapshot() const;
    QVariantList lastWorkspacesSnapshot() const;
    NiriKeyboardLayouts lastKeyboardLayouts() const;
    bool lastOverviewOpen() const;
    std::optional<quint64> lastFocusedWindowId() const;

signals:
    void rawEvent(const QString &name, const QVariant &payload);
    void decodeError(const QString &path, const QString &message, const QVariant &payload);
    void unknownEvent(const QString &name, const QVariant &payload);

private:
    explicit NiriEvents(QObject *parent = nullptr, NiriEvents *source = nullptr);
    void applyExistingState(const NiriEvent &event);
    NiriEvents *m_source = nullptr;

    QVariantList m_lastWindows;
    QVariantList m_lastWorkspaces;
    NiriKeyboardLayouts m_lastKeyboardLayouts;
    bool m_lastOverviewOpen = false;
    std::optional<quint64> m_lastFocusedWindowId;
};
