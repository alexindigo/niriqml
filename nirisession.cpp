#include "nirisession.h"

#include "nirilogging.h"
#include "niripeerinfo.h"
#include "generated/niriipcevents_generated.h"
#include <QCoreApplication>
#include <QLocalSocket>
#include <QJsonObject>
#include <QMetaEnum>
#include <QPointer>
#include <QThread>
#include <QScopeGuard>

struct NiriSession::Attempt : QObject
{
    Attempt(quint64 token, qsizetype limit, QObject *parent)
        : QObject(parent), epoch(token), framer(limit)
    {
        socket = new QLocalSocket(this);
        socket->setReadBufferSize(64 * 1024);
        deadline = new QTimer(this);
        deadline->setSingleShot(true);
    }
    ~Attempt() override { socket->abort(); }
    quint64 epoch;
    QLocalSocket *socket;
    QTimer *deadline;
    NiriLineBuffer framer;
    std::optional<NiriWire::Document> acknowledgement;
    bool draining = false;
    bool eof = false;
};

struct NiriSession::Snapshot
{
    Status status;
    QVariantMap error;
    PeerInfo peer;
    bool connected;
    bool subscribed;
    Endpoint endpoint;
    bool autoReconnect;
    bool initialStateReceived;
};

bool NiriSession::checkThread(const QObject *consumer)
{
    auto *app = QCoreApplication::instance();
    if (!app || QThread::currentThread() != app->thread()
        || (consumer && consumer->thread() != app->thread())) {
        // Deliberately not suppressible with our logging categories: unsupported
        // acquisition must diagnose before constructing/parenting any service.
        qWarning("niriqml: acquisition/use requires the QCoreApplication thread");
        return false;
    }
    return true;
}

NiriSession *NiriSession::instance()
{
    if (!checkThread())
        return nullptr;
    static QPointer<NiriSession> session;
    if (!session)
        session = new NiriSession(QCoreApplication::instance());
    return session;
}

NiriSession::NiriSession(QObject *parent) : QObject(parent), m_retry(new QTimer(this))
{
    m_announced = std::make_unique<Snapshot>(snapshot());
    m_retry->setSingleShot(true);
    connect(this, &NiriSession::streamReset, this, [this](quint64 token, const QString &reason) {
        const auto changes = m_state.reset(token, reason);
        emit stateUpdated(changes);
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        m_shuttingDown = true;
        stop();
    });
}

NiriSession::~NiriSession()
{
    m_shuttingDown = true;
    m_retry->stop();
    retireAttempt();
}

NiriSession::Endpoint NiriSession::currentEndpoint()
{
    if (!checkThread(this))
        return {};
    // A one-shot request may capture the endpoint before the queued automatic
    // stream start. Configuration resolution is independent of stream liveness.
    if (!m_endpointResolved)
        useEnvironmentEndpoint();
    return m_endpoint;
}
quint64 NiriSession::epoch() const
{
    return checkThread(this) ? m_epoch : 0;
}
NiriSession::Status NiriSession::status() const
{
    return checkThread(this) ? m_status : Failed;
}
bool NiriSession::isConnected() const
{
    return checkThread(this) && m_attempt
            && m_attempt->socket->state() == QLocalSocket::ConnectedState;
}
bool NiriSession::subscribed() const
{
    return checkThread(this) && m_status == Streaming;
}
std::optional<NiriWire::Document> NiriSession::subscriptionReply() const
{
    if (!subscribed() || !m_attempt)
        return std::nullopt;
    return m_attempt->acknowledgement;
}
bool NiriSession::autoReconnect() const
{
    return checkThread(this) && m_autoReconnect;
}
QVariantMap NiriSession::error() const
{
    return checkThread(this) ? m_error : QVariantMap{};
}
QString NiriSession::errorString() const
{
    return error().value("message").toString();
}
PeerInfo NiriSession::peerInfo() const
{
    return checkThread(this) ? m_peerInfo : PeerInfo{};
}

NiriSession::Snapshot NiriSession::snapshot() const
{
    return {m_status,     m_error,    m_peerInfo,      isConnected(),
            subscribed(), m_endpoint, m_autoReconnect, m_state.initialStateReceived()};
}

bool NiriSession::publish(quint64 token)
{
    // A reentrant transition must not lose NOTIFYs for fields already committed
    // by its caller. One publication owner reconciles the last announced values
    // against the *current* snapshot after every callback, never a stale diff.
    if (m_publishing)
        return token == m_epoch;
    m_publishing = true;
    bool superseded = false;
    for (;;) {
        const auto now = snapshot();
#define ANNOUNCE(field, signal)            \
    if (m_announced->field != now.field) { \
        m_announced->field = now.field;    \
        emit signal();                     \
        superseded |= token != m_epoch;    \
        continue;                          \
    }
        ANNOUNCE(endpoint.path, socketPathChanged)
        ANNOUNCE(endpoint.source, endpointSourceChanged)
        ANNOUNCE(endpoint.revision, endpointChanged)
        ANNOUNCE(autoReconnect, autoReconnectChanged)
        ANNOUNCE(status, statusChanged)
        ANNOUNCE(connected, connectedChanged)
        ANNOUNCE(subscribed, subscribedChanged)
        ANNOUNCE(error, errorChanged)
        ANNOUNCE(peer, peerInfoChanged)
        ANNOUNCE(initialStateReceived, initialStateReceivedChanged)
#undef ANNOUNCE
        break;
    }
    m_publishing = false;
    return !superseded && token == m_epoch;
}

void NiriSession::ensureStarted()
{
    if (!checkThread(this) || m_everStarted || m_shuttingDown)
        return;
    m_everStarted = true;
    m_wantedRunning = true;
    const auto token = m_epoch;
    // Even configuration notifications wait until all facades/receivers in the
    // acquisition stack are fully installed. Explicit selection queues its own
    // start; stop invalidates this token and cannot be undone by new consumers.
    QMetaObject::invokeMethod(
            this,
            [this, token] {
                if (token == m_epoch && m_wantedRunning)
                    start();
            },
            Qt::QueuedConnection);
}

void NiriSession::connectToSocket(const QString &path)
{
    if (!checkThread(this) || m_shuttingDown)
        return;
    m_everStarted = true;
    m_wantedRunning = true;
    // Intent is committed before selection notifications. A handler's stop()
    // wins; nothing after select() can restore the outer call's intent.
    if (path.isEmpty())
        useEnvironmentEndpoint();
    else
        selectEndpoint(path);
}

void NiriSession::start()
{
    if (!checkThread(this) || m_shuttingDown)
        return;
    m_everStarted = true;
    m_wantedRunning = true;
    if (!m_endpointResolved) {
        useEnvironmentEndpoint();
        return;
    }
    if (m_attempt || m_retry->isActive() || m_attemptQueued)
        return;
    queueAttempt();
}

void NiriSession::stop()
{
    if (!checkThread(this))
        return;
    m_everStarted = true; // Later consumers cannot implicitly resume.
    m_wantedRunning = false;
    m_retry->stop();
    retireAttempt();
    m_status = Stopped;
    m_reconnectAttempt = 0;
    m_peerInfo = {};
    const auto token = m_epoch;
    emit streamReset(token, QStringLiteral("Stopped"));
    if (token == m_epoch)
        publish(token);
}

void NiriSession::selectEndpoint(const QString &path)
{
    if (checkThread(this))
        select(path, Explicit);
}
void NiriSession::useEnvironmentEndpoint()
{
    if (checkThread(this))
        select(QString::fromUtf8(qgetenv("NIRI_SOCKET")), Environment);
}

void NiriSession::select(const QString &path, EndpointSource source)
{
    if (m_endpointResolved && m_endpoint.path == path && m_endpoint.source == source) {
        if (m_wantedRunning && !m_attempt && !m_retry->isActive() && !m_attemptQueued)
            queueAttempt();
        return;
    }
    m_endpointResolved = true;
    m_endpoint = {path, m_endpoint.revision + 1, source};
    m_retry->stop();
    retireAttempt();
    m_peerInfo = {};
    m_error.clear();
    m_status = Stopped;
    m_reconnectAttempt = 0;
    const auto token = m_epoch;
    // Queue before publishing so nested loops cannot observe a half-transition.
    if (m_wantedRunning)
        queueAttempt();
    emit streamReset(token, QStringLiteral("EndpointChanged"));
    if (token != m_epoch)
        return;
    publish(token);
}

void NiriSession::setAutoReconnect(bool enabled)
{
    if (!checkThread(this) || m_autoReconnect == enabled)
        return;
    m_autoReconnect = enabled;
    if (!enabled && m_retry->isActive()) {
        m_retry->stop();
        ++m_epoch;
        m_status = Failed;
    } else if (enabled && m_wantedRunning && !m_attempt && m_status == Failed
               && !m_endpoint.path.isEmpty()) {
        commitRetry();
    }
    const auto token = m_epoch;
    if (publish(token))
        notifyRetry(token);
}

void NiriSession::setConnectTimeoutMs(int ms)
{
    if (checkThread(this) && ms > 0)
        m_connectTimeoutMs = ms;
}
void NiriSession::setHandshakeTimeoutMs(int ms)
{
    if (checkThread(this) && ms > 0)
        m_handshakeTimeoutMs = ms;
}
void NiriSession::setMaxFrameBytes(qsizetype bytes)
{
    if (checkThread(this))
        m_maxFrameBytes = qMax<qsizetype>(0, bytes); // Applies to the next attempt.
}
qsizetype NiriSession::maxFrameBytes() const
{
    return checkThread(this) ? m_maxFrameBytes : 0;
}

void NiriSession::retireAttempt()
{
    ++m_epoch; // Invalidate before abort(), which may synchronously signal.
    m_attemptQueued = false;
    auto previous = std::move(m_attempt);
    if (previous) {
        previous->deadline->stop();
        previous->socket->abort();
    }
}

bool NiriSession::current(const std::shared_ptr<Attempt> &attempt) const
{
    return m_attempt == attempt && attempt->epoch == m_epoch && m_wantedRunning;
}

void NiriSession::queueAttempt()
{
    if (m_shuttingDown || !m_wantedRunning)
        return;
    if (m_attemptQueued)
        return;
    m_attemptQueued = true;
    const auto token = m_epoch;
    QMetaObject::invokeMethod(this, [this, token] { beginAttempt(token); }, Qt::QueuedConnection);
}

void NiriSession::beginAttempt(quint64 token)
{
    if (token != m_epoch || !m_wantedRunning || m_shuttingDown || m_attempt)
        return;
    m_attemptQueued = false;
    if (m_endpoint.path.isEmpty()) {
        fail(QStringLiteral("Configuration"), QStringLiteral("NoEndpoint"),
             QStringLiteral("No niri endpoint selected"));
        return;
    }
    retireAttempt();
    // Qt still touches QAbstractSocketPrivate after disconnected() returns.
    // Retirement disarms/aborts now, but destruction waits for signal stacks to
    // unwind. Parenting also releases retired contexts on application teardown
    // when no event loop was entered to process DeferredDelete.
    auto attempt = std::shared_ptr<Attempt>(new Attempt(m_epoch, m_maxFrameBytes, this),
                                            [](Attempt *a) { a->deleteLater(); });
    m_attempt = attempt;
    m_status = Connecting;
    m_peerInfo = {};
    std::weak_ptr<Attempt> weak = attempt;
    connect(attempt->socket, &QLocalSocket::connected, this, [this, weak] {
        if (auto a = weak.lock(); a && current(a))
            connected(a);
    });
    connect(attempt->socket, &QLocalSocket::readyRead, this, [this, weak] {
        if (auto a = weak.lock(); a && current(a))
            drain(a);
    });
    connect(attempt->socket, &QLocalSocket::disconnected, this, [this, weak] {
        if (auto a = weak.lock(); a && current(a)) {
            a->eof = true;
            drain(a);
        }
    });
    connect(attempt->socket, &QLocalSocket::errorOccurred, this,
            [this, weak](QLocalSocket::LocalSocketError error) {
                if (auto a = weak.lock(); a && current(a)) {
                    if (error == QLocalSocket::PeerClosedError)
                        return; // disconnected drains all complete frames first.
                    fail(QStringLiteral("Transport"), QStringLiteral("SocketError"),
                         a->socket->errorString());
                }
            });
    connect(attempt->deadline, &QTimer::timeout, this, [this, weak] {
        if (auto a = weak.lock(); a && current(a)) {
            const bool handshake = m_status == Subscribing;
            fail(QStringLiteral("Timeout"),
                 handshake ? QStringLiteral("HandshakeTimeout") : QStringLiteral("ConnectTimeout"),
                 handshake ? QStringLiteral("EventStream acknowledgment timed out")
                           : QStringLiteral("Connection timed out"));
        }
    });
    attempt->deadline->start(m_connectTimeoutMs);
    token = m_epoch;
    emit streamReset(token, QStringLiteral("Connecting"));
    if (token != m_epoch || !publish(token))
        return;
    qCDebug(niriTransport) << "Connecting to selected endpoint";
    attempt->socket->connectToServer(m_endpoint.path);
}

void NiriSession::connected(const std::shared_ptr<Attempt> &attempt)
{
    m_status = Subscribing;
    m_peerInfo = NiriPeerInfoReader::read(attempt->socket->socketDescriptor());
    attempt->deadline->start(m_handshakeTimeoutMs);
    // Subscription committed/sent before connectedChanged, never sent after a
    // handler has selected a replacement socket.
    const auto bytes = NiriWire::encode(NiriWire::WireValue(QStringLiteral("EventStream")));
    const QByteArray request = *bytes + '\n';
    if (attempt->socket->write(request) != request.size()) {
        fail(QStringLiteral("Transport"), QStringLiteral("WriteError"),
             QStringLiteral("Could not queue EventStream subscription"));
        return;
    }
    publish(attempt->epoch);
}

void NiriSession::drain(const std::shared_ptr<Attempt> &attempt)
{
    if (attempt->draining || m_deliveringEvent)
        return; // A nested loop cannot recursively drain this framer.
    attempt->draining = true;
    while (current(attempt) && attempt->socket->bytesAvailable() > 0) {
        const auto bytes = attempt->socket->read(64 * 1024);
        const bool ok = attempt->framer.feed(bytes, [this, &attempt](const QByteArray &frame) {
            if (current(attempt))
                processFrame(attempt, frame);
        });
        if (!current(attempt))
            break;
        if (!ok) {
            fail(QStringLiteral("Framing"), QStringLiteral("FrameTooLarge"),
                 QStringLiteral("Framing error: event frame exceeds %1 bytes")
                         .arg(attempt->framer.maxFrameBytes()));
            break;
        }
    }
    attempt->draining = false;
    if (current(attempt) && attempt->eof) {
        if (!attempt->framer.finish())
            fail(QStringLiteral("Framing"), QStringLiteral("TruncatedFrame"),
                 QStringLiteral("Event stream ended in a partial frame"));
        else
            fail(QStringLiteral("Transport"), QStringLiteral("UnexpectedEof"),
                 QStringLiteral("Event stream disconnected"));
    }
}

void NiriSession::processFrame(const std::shared_ptr<Attempt> &attempt, const QByteArray &frame)
{
    qCDebug(niriWireLog).noquote() << frame; // Explicit payload-tracing opt-in only.
    NiriWire::Error error;
    const auto document = NiriWire::parse(frame, &error);
    if (!current(attempt))
        return;
    if (!document) {
        const auto message =
                QStringLiteral("Invalid JSON at byte %1: %2").arg(error.offset).arg(error.reason);
        if (m_status == Subscribing)
            protocolError(attempt->epoch, message, frame);
        else
            fail(QStringLiteral("Parse"), QStringLiteral("InvalidJson"), message, frame);
        return;
    }
    const auto *members = document->root.objectValue();
    if (m_status == Subscribing) {
        const auto *ok = document->root.lookup(QStringLiteral("Ok"));
        if (!members || members->size() != 1 || !ok || !ok->stringValue()
            || *ok->stringValue() != QStringLiteral("Handled")) {
            const auto *rejected = document->root.lookup(QStringLiteral("Err"));
            if (members && members->size() == 1 && rejected && rejected->stringValue())
                fail(QStringLiteral("Server"), QStringLiteral("SubscriptionRejected"),
                     *rejected->stringValue(), frame);
            else
                protocolError(attempt->epoch, QStringLiteral("Expected exactly Ok/Handled"), frame);
            return;
        }
        attempt->acknowledgement = *document;
        m_status = Streaming;
        m_reconnectAttempt = 0; // Not reset at socket acceptance.
        m_error.clear();
        attempt->deadline->stop();
        emit subscriptionAccepted(attempt->epoch);
        if (current(attempt))
            publish(attempt->epoch);
        return;
    }
    if ((!members || members->size() != 1) && !document->root.stringValue()) {
        protocolError(attempt->epoch, QStringLiteral("Expected a single event variant"), frame);
        return;
    }
    applyEvent(*document, attempt->epoch);
}

void NiriSession::applyEvent(const NiriWire::Document &document, quint64 token)
{
    if (token != m_epoch || m_status != Streaming)
        return;
    m_deliveringEvent = true;
    const auto release = qScopeGuard([this] {
        m_deliveringEvent = false;
        // Resume a replacement attempt made readable by an observer's nested loop.
        if (auto attempt = m_attempt;
            attempt && (attempt->socket->bytesAvailable() || attempt->eof))
            QMetaObject::invokeMethod(
                    this,
                    [this, attempt] {
                        if (current(attempt))
                            drain(attempt);
                    },
                    Qt::QueuedConnection);
    });
    NiriEvent event;
    NiriIpc::DecodeError detail;
    const bool decoded = NiriIpc::decodeEvent(document.root, event, detail);
    if (decoded) {
        const auto applied = m_state.apply(event, token);
        if (applied.status == NiriEventStreamState::ApplyResult::ProtocolError) {
            protocolError(token, applied.error, document.bytes);
            return;
        }
        if (applied.status != NiriEventStreamState::ApplyResult::Applied)
            return;
        emit stateUpdated(applied.changes);
        if (token != m_epoch || !publish(token))
            return;
    }
    emit eventFrame(document, token);
    if (token != m_epoch || m_status != Streaming)
        return;
    const auto *unit = document.root.stringValue();
    const auto *object = document.root.objectValue();
    // Keep the legacy facade's observer ordering without sending its projection
    // back through a typed decoder. Both native handoffs share the parsed tree.
    const auto name = unit ? *unit : object->first().first;
    const auto payload =
            unit ? QVariantMap{} : NiriWire::toQmlProjection(object->first().second).toMap();
    emit eventReceived(name, payload, token);
    if (token != m_epoch || m_status != Streaming)
        return;
    emit eventPublication(document, token);
    if (token != m_epoch || m_status != Streaming)
        return;
    emit decodedEventPublication(document, event, detail.path, detail.message, token);
    // No work follows publication without returning to the drain's epoch check.
}

void NiriSession::protocolError(quint64 token, const QString &message, const QByteArray &raw)
{
    if (checkThread(this) && token == m_epoch)
        fail(QStringLiteral("Protocol"), QStringLiteral("ProtocolError"), message, raw);
}

void NiriSession::fail(const QString &category, const QString &code, const QString &message,
                       const QByteArray &raw)
{
    const QString phase = QString::fromLatin1(QMetaEnum::fromType<Status>().valueToKey(m_status));
    retireAttempt();
    m_retry->stop();
    m_peerInfo = {};
    m_error = {{"category", category},
               {"code", code},
               {"message", message},
               {"phase", phase},
               {"endpoint", m_endpoint.path},
               {"endpointRevision", QString::number(m_endpoint.revision)},
               {"epoch", QString::number(m_epoch)},
               {"raw", raw}};
    m_status = Failed;
    commitRetry();
    const auto token = m_epoch;
    qCDebug(niriProtocol) << code << message;
    emit streamReset(token, code);
    if (token == m_epoch && publish(token))
        notifyRetry(token);
}

void NiriSession::commitRetry()
{
    if (!m_autoReconnect || !m_wantedRunning || m_endpoint.path.isEmpty() || m_shuttingDown)
        return;
    m_status = Backoff;
    const int delay = qMin(30000, 500 << qMin(m_reconnectAttempt, 6));
    ++m_reconnectAttempt;
    ++m_retrySerial;
    const auto token = m_epoch;
    const auto revision = m_endpoint.revision;
    // Replace the old callback as well as disarming its timer.
    QObject::disconnect(m_retry, nullptr, this, nullptr);
    connect(m_retry, &QTimer::timeout, this, [this, token, revision] {
        if (token == m_epoch && revision == m_endpoint.revision && m_autoReconnect
            && m_wantedRunning && m_status == Backoff)
            beginAttempt(token);
    });
    m_retry->start(delay);
}

void NiriSession::notifyRetry(quint64 token)
{
    if (token == m_epoch && m_status == Backoff && m_retry->isActive()
        && m_retrySerial != m_announcedRetrySerial) {
        m_announcedRetrySerial = m_retrySerial;
        emit reconnectScheduled(m_retry->interval(), m_reconnectAttempt);
    }
}
