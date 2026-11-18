#include "nirirequestsequence.h"
#include "nirirequests.h"

NiriRequestSequence::NiriRequestSequence(QObject *scope, NiriRequestOperation::Options options,
                                         quint64 revision)
    : m_options(options),
      m_channel(NiriRequestOperation::channel(options.maxFrameBytes)),
      m_revision(revision)
{
    if (scope)
        connect(scope, &QObject::destroyed, this, [this] { stop(QStringLiteral("Cancelled")); });
    else {
        m_stopped = true;
        m_stopCategory = QStringLiteral("Cancelled");
    }
    connect(NiriSession::instance(), &NiriSession::endpointChanged, this, [this] {
        if (NiriSession::instance()->currentEndpoint().revision != m_revision)
            stop(QStringLiteral("EndpointChanged"));
    });
    connect(
            m_channel->socket, &QLocalSocket::disconnected, this,
            [this] { stop(QStringLiteral("Transport")); }, Qt::QueuedConnection);
}

NiriRequestSequence::~NiriRequestSequence()
{
    // No public signals during destruction. Entries remain guarded operations
    // until their queued cancellation delivery has unwound.
    if (m_active)
        m_active->cancel();
    for (auto operation : m_queue)
        if (operation)
            operation->cancel();
    m_channel->socket->abort();
}

NiriPendingReply *NiriRequestSequence::request(const QVariant &input, const QVariantMap &options)
{
    if (!NiriSession::checkThread(this))
        return nullptr;
    QString error;
    const auto wire = NiriRequests::wireValue(input, error);
    auto opts = m_options;
    if (error.isEmpty())
        NiriRequests::parseOptions(options, opts, error);
    NiriRequestOperation *operation = nullptr;
    auto *reply = NiriRequests::dispatch(wire.value_or(NiriWire::WireValue()), this, opts, nullptr,
                                         {}, error, m_channel, true, false, &operation);
    connect(operation, &NiriRequestOperation::settled, this, [this, operation](bool success) {
        if (m_active != operation) {
            if (!success && !m_stopped)
                stop(QStringLiteral("Cancelled"));
            return;
        }
        m_active = nullptr;
        if (!success) {
            stop(QStringLiteral("Cancelled"));
            return;
        }
        QMetaObject::invokeMethod(this, [this] { advance(); }, Qt::QueuedConnection);
    });
    if (m_stopped) {
        operation->notSent(m_stopCategory,
                           QStringLiteral("Sequence stopped before request was sent"));
    } else {
        m_queue.enqueue(operation);
        QMetaObject::invokeMethod(this, [this] { advance(); }, Qt::QueuedConnection);
    }
    return reply;
}

NiriPendingReply *NiriRequestSequence::action(const QVariant &action, const QVariantMap &options)
{
    return request(QVariantMap{{QStringLiteral("Action"), action}}, options);
}

void NiriRequestSequence::advance()
{
    if (m_stopped || m_active)
        return;
    if (m_channel->retired) {
        stop(QStringLiteral("Transport"));
        return;
    }
    while (!m_queue.isEmpty()) {
        m_active = m_queue.dequeue();
        if (m_active) {
            m_active->start();
            return;
        }
    }
}

void NiriRequestSequence::stop(const QString &category)
{
    if (m_stopped)
        return;
    m_stopped = true;
    m_stopCategory = category;
    if (m_active)
        m_active->cancel(category);
    while (!m_queue.isEmpty()) {
        auto operation = m_queue.dequeue();
        if (operation)
            operation->notSent(category,
                               QStringLiteral("Sequence stopped before request was sent"));
    }
    m_channel->socket->abort();
    emit stoppedChanged();
}

void NiriRequestSequence::cancel()
{
    if (NiriSession::checkThread(this))
        stop(QStringLiteral("Cancelled"));
}
