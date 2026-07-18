#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QObject>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>
#include <functional>

#include "niripendingreply.h"

class NiriRequests : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(NiriRequests)

public:
    using Callback = std::function<void(bool ok, const QJsonObject &result)>;

    static NiriRequests *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriRequests *instance();

    // Low-level primitive: send a JSON value (object or bare string) with a
    // C++ callback. Used internally by higher-level wrappers.
    void send(const QJsonObject &request, Callback callback);
    void sendJson(const QJsonValue &request, Callback callback);

    // Generic escape hatch: send an arbitrary bare-string query (e.g. "Version",
    // "OverviewState", or any future IPC name) and receive the raw result as
    // a QVariantMap. Use when you need to talk to niri IPC surfaces that
    // haven't been wrapped as typed methods yet.
    Q_INVOKABLE NiriPendingReply *sendRaw(const QString &query);
    Q_INVOKABLE NiriPendingReply *sendRaw(const QJsonObject &request);

    // Typed query wrappers. Each returns a NiriPendingReply.
    // On success, .value is a QVariantMap or gadget as documented per method.
    Q_INVOKABLE NiriPendingReply *windows(); // value: QVariantList of NiriWindow
    Q_INVOKABLE NiriPendingReply *workspaces(); // value: QVariantList of NiriWorkspace
    Q_INVOKABLE NiriPendingReply *outputs(); // value: QVariantMap<name, NiriOutput>
    Q_INVOKABLE NiriPendingReply *focusedWindow(); // value: NiriWindow (wrapped in QVariant)
    Q_INVOKABLE NiriPendingReply *focusedOutput(); // value: NiriOutput (wrapped in QVariant)
    Q_INVOKABLE NiriPendingReply *keyboardLayouts(); // value: NiriKeyboardLayouts (wrapped)
    Q_INVOKABLE NiriPendingReply *version(); // value: QString

private:
    explicit NiriRequests(QObject *parent = nullptr);

    struct PendingRequest
    {
        QLocalSocket *socket = nullptr;
        QByteArray sendBuffer;
        QByteArray recvBuffer;
        Callback callback;
        bool finished = false;
    };

    QString socketPath() const;
    void completeRequest(PendingRequest *pr, bool ok, const QJsonObject &result);
};
