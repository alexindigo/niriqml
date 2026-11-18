#include "nirimock.h"
#include <QFile>
#include <QJsonDocument>
#include <QTimer>

using namespace NiriWire;
namespace {
WireValue object(const QString &key, const WireValue &value)
{
    return *WireValue::object({{key, value}});
}
} // namespace

NiriMock::NiriMock(QObject *parent) : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, &NiriMock::accept);
    const QMap<QString, QString> kinds{{"Version", "response-1"},
                                       {"Outputs", "response-2"},
                                       {"Layers", "response-5"},
                                       {"KeyboardLayouts", "response-6"},
                                       {"FocusedOutput", "response-focused-output-null"},
                                       {"FocusedWindow", "response-focused-window-null"},
                                       {"PickWindow", "response-picked-window-null"},
                                       {"PickColor", "response-picked-color-null"},
                                       {"OverviewState", "response-12"},
                                       {"Casts", "response-13"},
                                       {"Output", "response-11"},
                                       {"Action", "response-0"}};
    for (auto it = kinds.begin(); it != kinds.end(); ++it)
        m_responses.insert(it.key(), fixture("Response", it.value()));
    m_responses.insert("Windows", object("Windows", m_windows));
    m_responses.insert("Workspaces", object("Workspaces", m_workspaces));
    m_responses.insert("Casts", object("Casts", WireValue(WireValue::Array{})));
}
NiriMock::~NiriMock()
{
    stop();
}
WireValue NiriMock::fixture(const QString &group, const QString &name)
{
    QFile file(QStringLiteral(NIRIQML_TEST_FIXTURES) + "/serde-v26.04/vectors.json");
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open released mock fixtures");
    const auto document = parse(file.readAll());
    if (!document)
        qFatal("Invalid released mock fixtures");
    for (const auto &entry : *document->root.lookup("surfaceVectors")->arrayValue()) {
        if (*entry.lookup("type")->stringValue() == group
            && *entry.lookup("name")->stringValue() == name)
            return *entry.lookup("value");
    }
    qFatal("Missing released mock fixture: %s/%s", qPrintable(group), qPrintable(name));
}
bool NiriMock::start()
{
    stop();
    return m_directory.isValid() && m_server.listen(m_directory.filePath("ipc.sock"));
}
void NiriMock::stop()
{
    const auto clients = m_clients;
    m_clients.clear();
    for (auto client : clients) {
        if (client) {
            client->socket->abort();
            client->deleteLater();
        }
    }
    m_server.close();
}
QString NiriMock::socketPath() const
{
    return m_server.fullServerName();
}
void NiriMock::setWindows(const QJsonArray &windows)
{
    m_windows = fromQJsonValueLossy(windows).value();
    m_responses.insert("Windows", object("Windows", m_windows));
}
void NiriMock::setWorkspaces(const QJsonArray &workspaces)
{
    m_workspaces = fromQJsonValueLossy(workspaces).value();
    m_responses.insert("Workspaces", object("Workspaces", m_workspaces));
}
void NiriMock::setKeyboardLayouts(const std::optional<WireValue> &layouts)
{
    m_layouts = layouts;
}
void NiriMock::setNextActionError(const QString &message)
{
    m_nextActionError = message;
}
void NiriMock::enqueueFault(const Fault &fault)
{
    m_faults.enqueue(fault);
}
void NiriMock::setResponse(const QString &request, const WireValue &response)
{
    m_responses.insert(request, response);
}
QByteArray NiriMock::line(const WireValue &value)
{
    return encode(value).value() + '\n';
}
void NiriMock::sendEvent(const QJsonObject &event)
{
    sendEventBytes(QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n');
}
void NiriMock::sendEventBytes(const QByteArray &bytes)
{
    for (auto client : m_clients)
        if (client && client->broadcastReady && client->socket
            && client->socket->state() == QLocalSocket::ConnectedState)
            client->socket->write(bytes);
}
int NiriMock::streamCount() const
{
    int count = 0;
    for (auto client : m_clients)
        if (client && client->broadcastReady && client->socket
            && client->socket->state() == QLocalSocket::ConnectedState)
            ++count;
    return count;
}
void NiriMock::accept()
{
    while (auto *socket = m_server.nextPendingConnection()) {
        auto *client = new Client(this);
        client->id = ++m_nextClient;
        client->socket = socket;
        socket->setParent(client);
        m_clients.append(client);
        connect(socket, &QLocalSocket::readyRead, client, [this, client] { read(client); });
        connect(socket, &QLocalSocket::disconnected, client, [this, client] {
            const bool stream = client->broadcastReady;
            client->broadcastReady = false;
            m_clients.removeAll(client);
            client->deleteLater();
            if (stream)
                emit disconnected();
        });
    }
}
void NiriMock::read(Client *client)
{
    if (client->stream) {
        // The released server stops reading requests after EventStream.
        client->socket->readAll();
        return;
    }
    client->input += client->socket->readAll();
    while (client->scan < client->input.size()) {
        if (client->input.at(client->scan++) != '\n')
            continue;
        const auto bytes = client->input.left(client->scan - 1);
        client->input.remove(0, client->scan);
        client->scan = 0;
        QPointer<Client> guard(client);
        request(client, bytes);
        if (!guard)
            return;
        if (client->stream) {
            client->input.clear();
            return;
        }
    }
}
QByteArray NiriMock::initialBurst() const
{
    QByteArray bytes = line(object("WorkspacesChanged", object("workspaces", m_workspaces)))
            + line(object("WindowsChanged", object("windows", m_windows)));
    if (m_layouts)
        bytes += line(object("KeyboardLayoutsChanged", object("keyboard_layouts", *m_layouts)));
    bytes += line(fixture("Event", "event-OverviewOpenedOrClosed"));
    bytes += line(fixture("Event", "event-ConfigLoaded"));
    bytes += line(object("CastsChanged", object("casts", WireValue(WireValue::Array{}))));
    return bytes;
}
void NiriMock::request(Client *client, const QByteArray &bytes)
{
    emit requestLine(client->id, bytes);
    const auto document = parse(bytes);
    if (!document) {
        client->socket->disconnectFromServer();
        return;
    }
    QString kind;
    if (document->root.stringValue())
        kind = *document->root.stringValue();
    else if (document->root.objectValue() && document->root.objectValue()->size() == 1) {
        kind = document->root.objectValue()->first().first;
        emit requestReceived(QJsonDocument::fromJson(bytes).object());
    }
    const Fault fault = m_faults.isEmpty() ? Fault{} : m_faults.dequeue();
    QByteArray response;
    if (kind == "EventStream") {
        client->stream = true;
        response = line(object("Ok", m_responses.value("Action"))) + initialBurst();
    } else if (kind == "ReturnError") {
        response = line(object("Err", WireValue("example error")));
    } else if (kind == "Action" && !m_nextActionError.isEmpty()) {
        response = line(object("Err", WireValue(m_nextActionError)));
        m_nextActionError.clear();
    } else if (m_responses.contains(kind)) {
        response = line(object("Ok", m_responses.value(kind)));
    } else {
        response = line(object("Err", WireValue("unknown request")));
    }
    reply(client, fault.bytes.isEmpty() ? response : fault.bytes, fault, kind == "EventStream");
}
void NiriMock::reply(Client *client, const QByteArray &bytes, const Fault &fault,
                     bool activateStream)
{
    QTimer::singleShot(fault.delayMs, client, [this, client, bytes, fault, activateStream] {
        if (!client->socket || client->socket->state() != QLocalSocket::ConnectedState)
            return;
        if (!fault.drop) {
            qsizetype offset = 0;
            for (const auto size : fault.fragments) {
                const auto fragment = bytes.mid(offset, size);
                if (client->socket->write(fragment) != fragment.size())
                    return;
                client->socket->flush();
                offset += size;
            }
            const auto remaining = bytes.mid(offset);
            if (client->socket->write(remaining) != remaining.size())
                return;
        }
        // Released niri accepts further requests on the same connection.
        if (fault.close) {
            client->socket->disconnectFromServer();
            return;
        }
        if (activateStream && !fault.drop) {
            client->broadcastReady = true;
            emit connected();
        }
    });
}
