#pragma once

#include "nirirequestoperation.h"
#include <QQueue>

// One persistent socket per explicit sequence, never a global FIFO. A sequence
// is not a transaction or an event-stream/animation convergence barrier.
class NiriRequestSequence : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriRequestSequence)
    QML_UNCREATABLE("Use NiriRequests.sequence()")
    Q_PROPERTY(bool stopped READ stopped NOTIFY stoppedChanged)
public:
    NiriRequestSequence(QObject *scope, NiriRequestOperation::Options options, quint64 revision);
    ~NiriRequestSequence() override;
    Q_INVOKABLE NiriPendingReply *request(const QVariant &request, const QVariantMap &options = {});
    Q_INVOKABLE NiriPendingReply *action(const QVariant &action, const QVariantMap &options = {});
    Q_INVOKABLE void cancel();
    bool stopped() const { return m_stopped; }
signals:
    void stoppedChanged();

private:
    void advance();
    void stop(const QString &category);
    NiriRequestOperation::Options m_options;
    NiriRequestOperation::Channel m_channel;
    QQueue<QPointer<NiriRequestOperation>> m_queue;
    QPointer<NiriRequestOperation> m_active;
    bool m_stopped = false;
    QString m_stopCategory = QStringLiteral("Cancelled");
    quint64 m_revision;
};
