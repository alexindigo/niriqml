#pragma once

#include "nirisession.h"
#include "socketdirectory.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTest>

// Deliberately no automatic initial burst: tests observe each exact prefix.
class StateStreamFixture
{
public:
    StateStreamFixture()
    {
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QLocalSocket::readyRead, &server, [this, socket] {
                while (socket->canReadLine()) {
                    const auto line = socket->readLine();
                    if (line == "\"EventStream\"\n") {
                        peer = socket;
                        request = line;
                        socket->write("{\"Ok\":\"Handled\"}\n");
                    } else {
                        ++queries;
                        socket->write(queryReply + '\n');
                        socket->disconnectFromServer();
                    }
                }
            });
        });
    }
    ~StateStreamFixture()
    {
        NiriSession::instance()->stop();
        NiriSession::instance()->setAutoReconnect(oldPolicy);
    }
    bool start()
    {
        auto *session = NiriSession::instance();
        oldPolicy = session->autoReconnect();
        session->stop();
        session->setAutoReconnect(false);
        if (!directory.isValid() || !server.listen(directory.filePath("socket")))
            return false;
        session->connectToSocket(server.fullServerName());
        return QTest::qWaitFor([=] { return session->subscribed(); });
    }
    void send(const QByteArray &bytes)
    {
        peer->write(bytes + '\n');
        peer->flush();
    }
    SocketDirectory directory;
    QLocalServer server;
    QPointer<QLocalSocket> peer;
    QByteArray request;
    QByteArray queryReply = "{\"Ok\":{\"Windows\":[]}}";
    int queries = 0;
    bool oldPolicy = true;
};
