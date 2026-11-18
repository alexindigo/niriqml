#pragma once

#include "nirirequestoperation.h"
#include "nirirequestsequence.h"
#include "generated/niriipcrequests_generated.h"
#include <QJsonObject>
#include <QQmlEngine>

class NiriRequests : public NiriIpcRequests
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(NiriRequests)
public:
    using Callback = std::function<void(bool, const QJsonObject &)>;
    static NiriRequests *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriRequests *instance();
    // Compatibility callbacks are lossy object projections. New C++ callers
    // should use sendWire() and inspect reply->wireDocument().
    void send(const QJsonObject &request, Callback callback);
    void sendJson(const QJsonValue &request, Callback callback);
    Q_INVOKABLE NiriPendingReply *sendRaw(const QVariant &request, const QVariantMap &options = {});
    NiriPendingReply *sendRaw(const QJsonObject &request);
    Q_INVOKABLE NiriPendingReply *sendRawJson(const QString &json, const QVariantMap &options = {});
    NiriPendingReply *sendWire(const NiriWire::WireValue &request,
                               NiriRequestOperation::Options options, QObject *owner);
    Q_INVOKABLE NiriRequestSequence *sequence();
    // Exact-number builder for structured raw input, including unknown fields.
    // Invalid text returns undefined; sending it produces a queued error.
    Q_INVOKABLE QVariant number(const QString &text);
    void setRequestTimeoutMs(int ms)
    {
        if (NiriSession::checkThread(this))
            m_requestTimeoutMs = ms;
    }
    int requestTimeoutMs() const { return NiriSession::checkThread(this) ? m_requestTimeoutMs : 0; }
    void setMaxFrameBytes(qsizetype bytes)
    {
        if (NiriSession::checkThread(this))
            m_maxFrameBytes = qMax<qsizetype>(0, bytes);
    }
    qsizetype maxFrameBytes() const { return NiriSession::checkThread(this) ? m_maxFrameBytes : 0; }

    // Shared entry point for actions, sequences and generated adapters. The
    // scope cancels work on engine/facade teardown but never parents JS replies.
    static NiriPendingReply *
    dispatch(const NiriWire::WireValue &request, QObject *scope,
             NiriRequestOperation::Options options, QObject *owner = nullptr,
             NiriRequestOperation::Decoder decoder = {}, const QString &validationError = {},
             NiriRequestOperation::Channel channel = {}, bool persistent = false, bool start = true,
             NiriRequestOperation **operationOut = nullptr,
             std::function<NiriPendingReply *(QObject *)> replyFactory = {});
    static NiriPendingReply *dispatchTyped(const NiriWire::WireValue &request, QObject *scope,
                                           NiriRequestOperation::Options options,
                                           const QString &expected,
                                           const QString &validationError = {});
    static std::optional<NiriWire::WireValue> wireValue(const QVariant &value, QString &error,
                                                        int depth = 0);
    static bool parseOptions(const QVariantMap &input, NiriRequestOperation::Options &options,
                             QString &error);

private:
    explicit NiriRequests(QObject *parent = nullptr);
    NiriRequestOperation::Options options() const;
    NiriPendingReply *submitRequest(const NiriWire::WireValue &request, const QString &expected,
                                    const QVariantMap &options, bool interactive, bool eventStream,
                                    const QString &validationError) override;
    int m_requestTimeoutMs = 5000;
    qsizetype m_maxFrameBytes = NiriLineBuffer::DefaultMaxFrameBytes;
};
