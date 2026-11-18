#pragma once

#include "nirilinebuffer.h"
#include "niripendingreply.h"
#include "nirisession.h"
#include <QLocalSocket>
#include <QTimer>
#include <functional>
#include <memory>

// Shared only by entries in one explicit sequence. Independent calls each own
// their own channel. There is exactly one active reader/writer per channel.
struct NiriRequestChannel : QObject
{
    explicit NiriRequestChannel(qsizetype maxFrameBytes);
    QLocalSocket *socket;
    NiriLineBuffer framer;
    bool retired = false;
};

class NiriRequestOperation : public QObject
{
    Q_OBJECT
public:
    struct Options
    {
        int timeoutMs = 5000; // -1 means no total deadline.
        int connectTimeoutMs = 5000;
        qsizetype maxFrameBytes = NiriLineBuffer::DefaultMaxFrameBytes;
        bool observeEventStream = false; // Observe the session; never allocate a request socket.
    };
    using Decoder = std::function<bool(const NiriWire::WireValue &, QVariant &, NiriError &)>;
    using Channel = std::shared_ptr<NiriRequestChannel>;
    static Channel channel(qsizetype maxFrameBytes);
    NiriRequestOperation(NiriPendingReply *reply, QObject *scope, NiriSession::Endpoint endpoint,
                         NiriWire::WireValue request, Options options, Decoder decoder = {},
                         Channel channel = {}, bool persistent = false,
                         QString validationError = {});
    void start(); // Queued, including all validation and configuration errors.
    void cancel(const QString &category = QStringLiteral("Cancelled"));
    void notSent(const QString &category, const QString &message);
signals:
    void settled(bool success); // Internal scheduling, before public completion.
private:
    void begin();
    void write();
    void drain(bool eof = false);
    void frame(const QByteArray &bytes);
    void consumeReply(const NiriWire::Document &document);
    void beginStream();
    void fail(const QString &category, const QString &message, int socketError = -1,
              qint64 offset = -1);
    void finish();
    QPointer<NiriPendingReply> m_reply;
    NiriSession::Endpoint m_endpoint;
    QString m_id;
    NiriWire::WireValue m_request;
    Options m_options;
    Decoder m_decoder;
    Channel m_channel;
    QTimer m_deadline, m_connectDeadline;
    QByteArray m_bytes;
    qsizetype m_written = 0;
    bool m_persistent = false, m_terminal = false, m_started = false, m_draining = false;
    QString m_validationError;
    QString m_certainty = QStringLiteral("NotSent");
    QString m_phase = QStringLiteral("Queued");
    NiriError m_error;
    NiriWire::Document m_document;
    QVariant m_value;
    QString m_responseKind;
};
