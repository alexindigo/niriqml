#include "nirimock.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>

NiriMock::NiriMock(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, &NiriMock::onNewConnection);
}

NiriMock::~NiriMock()
{
    stop();
}

bool NiriMock::start()
{
    QString path = QDir::tempPath() + QStringLiteral("/niriqml-test-") + QString::number(QCoreApplication::applicationPid());

    if (m_server.isListening())
        m_server.close();

    QLocalServer::removeServer(path);
    bool ok = m_server.listen(path);
    if (ok)
        m_socketPath = path;
    return ok;
}

void NiriMock::stop()
{
    if (m_eventSocket) {
        m_eventSocket->disconnectFromServer();
        m_eventSocket->deleteLater();
        m_eventSocket = nullptr;
    }
    if (m_server.isListening()) {
        QString path = m_server.fullServerName();
        m_server.close();
        QLocalServer::removeServer(path);
    }
    m_socketPath.clear();
}

QString NiriMock::socketPath() const { return m_socketPath; }

void NiriMock::setWindows(const QJsonArray &windows) { m_windows = windows; }
void NiriMock::setWorkspaces(const QJsonArray &workspaces) { m_workspaces = workspaces; }
void NiriMock::setNextActionError(const QString &message) { m_nextActionError = message; }

void NiriMock::sendEvent(const QJsonObject &event)
{
    if (!m_eventSocket)
        return;
    // Event is already {"EventName":{...}} — send as-is
    sendLine(m_eventSocket, event);
}

void NiriMock::onNewConnection()
{
    QLocalSocket *socket = m_server.nextPendingConnection();
    if (!socket)
        return;

    connect(socket, &QLocalSocket::readyRead, this, &NiriMock::onReadyRead);
    connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
}

void NiriMock::onReadyRead()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket)
        return;

    m_readBuffer += socket->readAll();

    while (true) {
        int idx = m_readBuffer.indexOf('\n');
        if (idx < 0)
            break;

        QByteArray line = m_readBuffer.left(idx).trimmed();
        m_readBuffer.remove(0, idx + 1);

        if (line.isEmpty())
            continue;

        // Handle bare JSON string requests (e.g. "EventStream")
        if (line.startsWith('"')) {
            QString requestStr = QString::fromUtf8(line.mid(1, line.size() - 2));
            if (requestStr == "EventStream") {
                handleEventStreamSubscribe(socket);
                continue;
            }
            continue;
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError)
            continue;

        QJsonObject obj = doc.object();
        emit requestReceived(obj);

        QJsonObject request = obj["Request"].toObject();
        if (!request.isEmpty()) {
            handleRequest(socket, request);
            continue;
        }

        // Might be an Action
        QJsonObject action = obj["Action"].toObject();
        if (!action.isEmpty()) {
            if (!m_nextActionError.isEmpty()) {
                sendLine(socket, QJsonObject{{"Err", m_nextActionError}});
                m_nextActionError.clear();
            } else {
                sendLine(socket, QJsonObject{{"Ok", QJsonObject{{"Handled", QJsonValue::Null}}}});
            }
            socket->disconnectFromServer();
            continue;
        }
    }
}

void NiriMock::handleEventStreamSubscribe(QLocalSocket *socket)
{
    m_eventSocket = socket;

    // First reply: {"Ok":"Handled"}
    sendLine(socket, QJsonObject{{"Ok", QStringLiteral("Handled")}});

    // Then send initial state as bare events {"EventName":{...}}
    for (const auto &w : m_windows) {
        QJsonObject event;
        event["WindowOpenedOrChanged"] = QJsonObject{{"window", w.toObject()}};
        sendLine(socket, event);
    }
    if (!m_windows.isEmpty()) {
        QJsonObject event;
        event["WindowsChanged"] = QJsonObject{{"windows", m_windows}};
        sendLine(socket, event);
    }
    if (!m_workspaces.isEmpty()) {
        QJsonObject event;
        event["WorkspacesChanged"] = QJsonObject{{"workspaces", m_workspaces}};
        sendLine(socket, event);
    }

    emit connected();
}

void NiriMock::handleRequest(QLocalSocket *socket, const QJsonObject &request)
{
    Q_UNUSED(request)
    // One-shot request: send canned reply, close
    sendLine(socket, QJsonObject{{"Ok", QJsonObject{{"Handled", QJsonValue::Null}}}});
    socket->disconnectFromServer();
}

void NiriMock::sendLine(QLocalSocket *socket, const QJsonObject &obj)
{
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    socket->write(data);
    socket->flush();
}
