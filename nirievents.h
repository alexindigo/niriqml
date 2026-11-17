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

    // Compatibility aliases over CURRENT reducer state, not last full events.
    Q_INVOKABLE QVariantList lastWindowsSnapshot() const;
    Q_INVOKABLE QVariantList lastWorkspacesSnapshot() const;
    std::optional<NiriKeyboardLayouts> lastKeyboardLayouts() const;
    Q_INVOKABLE QVariant qmlLastKeyboardLayouts() const;
    Q_INVOKABLE bool lastOverviewOpen() const;
    std::optional<quint64> lastFocusedWindowId() const;
    Q_INVOKABLE QVariant qmlLastFocusedWindowId() const;
    Q_INVOKABLE QVariantList lastCastsSnapshot() const;
    Q_INVOKABLE bool lastConfigFailed() const;

signals:
    void rawEvent(const QString &name, const QVariant &payload);
    void decodeError(const QString &path, const QString &message, const QVariant &payload);
    void unknownEvent(const QString &name, const QVariant &payload);

private:
    explicit NiriEvents(QObject *parent = nullptr, NiriEvents *source = nullptr);
    void publishDecoded(const NiriWire::Document &document, const NiriEvent &event,
                        const QString &errorPath, const QString &errorMessage, quint64 token);
    NiriEvents *m_source = nullptr;
};
