#include "nirirequestoperation.h"
#include <QCoreApplication>

using namespace NiriWire;

NiriRequestChannel::NiriRequestChannel(qsizetype maxFrameBytes)
    : socket(new QLocalSocket(this)), framer(maxFrameBytes)
{
    socket->setReadBufferSize(64 * 1024);
    connect(socket, &QLocalSocket::disconnected, this, [this] { retired = true; });
}

NiriRequestOperation::Channel NiriRequestOperation::channel(qsizetype maxFrameBytes)
{
    // Qt socket callbacks can still access their sender after emitting a signal.
    return Channel(new NiriRequestChannel(maxFrameBytes), [](auto *p) {
        if (QCoreApplication::closingDown())
            delete p;
        else
            p->deleteLater();
    });
}

NiriRequestOperation::NiriRequestOperation(NiriPendingReply *reply, QObject *scope,
                                           NiriSession::Endpoint endpoint, WireValue request,
                                           Options options, Decoder decoder, Channel shared,
                                           bool persistent, QString validationError)
    : QObject(NiriSession::instance()),
      m_reply(reply),
      m_endpoint(std::move(endpoint)),
      m_request(std::move(request)),
      m_options(options),
      m_decoder(std::move(decoder)),
      m_channel(options.observeEventStream ? Channel{}
                        : shared           ? std::move(shared)
                                           : channel(options.maxFrameBytes)),
      m_persistent(persistent),
      m_validationError(std::move(validationError))
{
    static quint64 nextId = 0; // Only the enforced application thread creates operations.
    m_id = QString::number(++nextId);
    reply->m_operation = this;
    reply->m_endpoint = m_endpoint.path;
    reply->m_revision = QString::number(m_endpoint.revision);
    reply->m_id = m_id;
    m_deadline.setSingleShot(true);
    m_connectDeadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] { fail("Timeout", "Timeout"); });
    connect(&m_connectDeadline, &QTimer::timeout, this,
            [this] { fail("Timeout", "Connection timeout"); });
    connect(NiriSession::instance(), &NiriSession::endpointChanged, this, [this] {
        if (NiriSession::instance()->currentEndpoint().revision != m_endpoint.revision)
            cancel(QStringLiteral("EndpointChanged"));
    });
    if (scope)
        connect(scope, &QObject::destroyed, this, [this] { cancel(); });
    else
        cancel();
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
            [this] { cancel(); });
}

void NiriRequestOperation::start()
{
    if (m_started || m_terminal)
        return;
    m_started = true;
    QMetaObject::invokeMethod(this, [this] { begin(); }, Qt::QueuedConnection);
}

void NiriRequestOperation::begin()
{
    if (m_terminal)
        return;
    if (!m_validationError.isEmpty()) {
        fail("Configuration", m_validationError);
        return;
    }
    if (m_options.timeoutMs < -1 || m_options.connectTimeoutMs < 1) {
        fail("Configuration", "Invalid request deadline");
        return;
    }
    if (m_endpoint.path.isEmpty()) {
        fail("Configuration", "No endpoint selected");
        return;
    }
    if (m_options.observeEventStream) {
        beginStream();
        return;
    }
    if (m_channel->retired) {
        fail("Transport", "Ordered sequence connection was closed; request was not sent");
        return;
    }
    Error error;
    const auto encoded = encode(m_request, &error);
    if (!encoded) {
        fail("Configuration", error.reason, -1, error.offset);
        return;
    }
    m_bytes = *encoded + '\n';
    auto *socket = m_channel->socket;
    connect(socket, &QLocalSocket::connected, this, [this] {
        m_connectDeadline.stop();
        write();
    });
    connect(socket, &QLocalSocket::bytesWritten, this, [this] { write(); });
    connect(socket, &QLocalSocket::readyRead, this, [this] { drain(); });
    connect(socket, &QLocalSocket::disconnected, this, [this] { drain(true); });
    connect(socket, &QLocalSocket::errorOccurred, this, [this](auto error) {
        // PeerClosed can precede readyRead/disconnected for the final bytes.
        if (error == QLocalSocket::PeerClosedError)
            drain(true);
        else
            fail("Transport", m_channel->socket->errorString(), int(error));
    });
    if (m_options.timeoutMs >= 0)
        m_deadline.start(m_options.timeoutMs);
    if (socket->state() == QLocalSocket::ConnectedState) {
        if (socket->bytesAvailable() || m_channel->framer.pendingBytes()) {
            fail("Protocol", "Unsolicited data on ordered sequence");
            return;
        }
        write();
    } else {
        m_phase = QStringLiteral("Connecting");
        m_connectDeadline.start(m_options.connectTimeoutMs);
        socket->connectToServer(m_endpoint.path);
    }
}

void NiriRequestOperation::write()
{
    if (m_terminal || m_written == m_bytes.size())
        return;
    m_phase = QStringLiteral("Sending");
    const auto count =
            m_channel->socket->write(m_bytes.constData() + m_written, m_bytes.size() - m_written);
    if (count < 0) {
        fail("Transport", m_channel->socket->errorString(), int(m_channel->socket->error()));
        return;
    }
    if (count > 0) {
        m_written += count;
        m_certainty = QStringLiteral("MayHaveExecuted");
    }
    if (m_written == m_bytes.size())
        m_phase = QStringLiteral("AwaitingReply");
}

void NiriRequestOperation::beginStream()
{
    if (!m_request.stringValue() || *m_request.stringValue() != QStringLiteral("EventStream")) {
        fail("Configuration", "Only EventStream can observe the session subscription");
        return;
    }
    auto *session = NiriSession::instance();
    m_phase = QStringLiteral("AwaitingSubscription");
    auto accepted = [this, session] {
        if (m_terminal || session->currentEndpoint().revision != m_endpoint.revision)
            return;
        if (const auto reply = session->subscriptionReply())
            consumeReply(*reply);
    };
    connect(session, &NiriSession::subscriptionAccepted, this, [accepted, session](quint64 epoch) {
        if (epoch == session->epoch())
            accepted();
    });
    connect(session, &NiriSession::statusChanged, this, [this, session] {
        if (m_terminal)
            return;
        if (session->isConnected())
            m_certainty = QStringLiteral("MayHaveExecuted");
        if (session->status() == NiriSession::Stopped) {
            cancel();
            return;
        }
        if (session->status() == NiriSession::Failed || session->status() == NiriSession::Backoff) {
            const auto detail = session->error();
            const auto raw = detail.value(QStringLiteral("raw")).toByteArray();
            if (!raw.isEmpty()) {
                m_document.bytes = raw;
                if (const auto parsed = NiriWire::parse(raw))
                    m_document = *parsed;
                m_certainty = QStringLiteral("ReplyReceived");
            }
            fail(detail.value(QStringLiteral("category"), "Transport").toString(),
                 detail.value(QStringLiteral("message"), "Subscription failed").toString());
        }
    });
    if (m_options.timeoutMs >= 0)
        m_deadline.start(m_options.timeoutMs);
    m_connectDeadline.start(m_options.connectTimeoutMs);
    session->start();
    if (!m_terminal)
        accepted(); // Retain the actual current acknowledgement for late callers.
}

void NiriRequestOperation::drain(bool eof)
{
    if (m_terminal || m_draining)
        return;
    m_draining = true;
    auto *socket = m_channel->socket;
    while (!m_terminal && socket->bytesAvailable()) {
        const auto bytes = socket->read(qMin<qint64>(socket->bytesAvailable(), 64 * 1024));
        if (bytes.isEmpty())
            break;
        const bool ok = m_channel->framer.feed(bytes, [this](const auto &bytes) {
            if (!m_terminal)
                frame(bytes);
            else if (m_persistent)
                m_channel->retired = true; // An unsolicited second reply has no FIFO owner.
        });
        if (!ok && !m_terminal)
            fail("Framing",
                 QStringLiteral("Framing error: frame exceeds %1 bytes")
                         .arg(m_channel->framer.error().allowedBytes));
        // The first completed reply still wins, but an ordered lane must never
        // carry trailing/unsolicited bytes into another request's correlation.
        if (m_terminal && m_persistent
            && (!ok || m_channel->retired || m_channel->framer.pendingBytes())) {
            m_channel->retired = true;
            socket->abort();
        }
    }
    if (!m_terminal && eof) {
        if (!m_channel->framer.finish())
            fail("Framing", "Framing error: truncated frame");
        else
            fail("Transport", "Unexpected EOF", int(QLocalSocket::PeerClosedError));
    }
    m_draining = false;
}

void NiriRequestOperation::frame(const QByteArray &bytes)
{
    m_certainty = QStringLiteral("ReplyReceived");
    m_phase = QStringLiteral("Reply");
    Error error;
    const auto parsed = parse(bytes, &error);
    if (!parsed) {
        m_document.bytes = bytes;
        fail("Parse", error.reason, -1, error.offset);
        return;
    }
    consumeReply(*parsed);
}

void NiriRequestOperation::consumeReply(const Document &document)
{
    if (m_terminal)
        return;
    m_certainty = QStringLiteral("ReplyReceived");
    m_phase = QStringLiteral("Reply");
    m_document = document;
    const auto &root = m_document.root;
    const auto *members = root.objectValue();
    const auto *ok = root.lookup(QStringLiteral("Ok"));
    const auto *err = root.lookup(QStringLiteral("Err"));
    if (!members || members->size() != 1 || (!ok && !err) || (err && !err->stringValue())) {
        fail("Protocol", "Reply must contain exactly one Ok or string Err");
        return;
    }
    if (err) {
        fail("Server", *err->stringValue());
        return;
    }
    if (ok->stringValue())
        m_responseKind = *ok->stringValue();
    else if (ok->objectValue() && ok->objectValue()->size() == 1)
        m_responseKind = ok->objectValue()->first().first;
    m_value = toQmlProjection(*ok);
    if (m_decoder && !m_decoder(*ok, m_value, m_error)) {
        const auto path = m_error.fieldPath;
        fail("Decode", m_error.message);
        m_error.fieldPath = path;
        return;
    }
    finish();
}

void NiriRequestOperation::cancel(const QString &category)
{
    fail(category, category);
}

void NiriRequestOperation::notSent(const QString &category, const QString &message)
{
    fail(category, message);
}

void NiriRequestOperation::fail(const QString &category, const QString &message, int socketError,
                                qint64 offset)
{
    if (m_terminal)
        return;
    m_error.code = -1;
    m_error.category = category;
    m_error.message = message;
    m_error.socketError = socketError;
    m_error.byteOffset = offset;
    m_error.certainty = m_certainty;
    m_error.requestId = m_id;
    m_error.endpoint = m_endpoint.path;
    m_error.endpointRevision = QString::number(m_endpoint.revision);
    m_error.phase = m_phase;
    finish();
}

void NiriRequestOperation::finish()
{
    if (m_terminal)
        return;
    m_terminal = true;
    m_deadline.stop();
    m_connectDeadline.stop();
    if (m_channel) {
        auto *socket = m_channel->socket;
        QObject::disconnect(socket, nullptr, this, nullptr);
        if (!m_persistent || !m_error.category.isEmpty())
            socket->abort();
    }
    // Commit once, then publish on a later turn, including cancel-before-start.
    QMetaObject::invokeMethod(
            this,
            [this] {
                emit settled(m_error.category.isEmpty());
                auto reply = m_reply;
                deleteLater();
                if (reply)
                    reply->complete(m_value, m_error, m_document, m_responseKind, m_certainty);
            },
            Qt::QueuedConnection);
}
