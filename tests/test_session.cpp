#include "niriactions.h"
#include "niriconnection.h"
#include "nirieventlog.h"
#include "nirievents.h"
#include "niripeerinfo.h"
#include "nirirequests.h"
#include "nirisession.h"
#include "niristate.h"
#include "niriwindowreactive.h"
#include "niriwindowsmodel.h"
#include "niriworkspacereactive.h"
#include "niriworkspacesmodel.h"

#include <QGuiApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <cerrno>
#include <cstring>
#ifdef Q_OS_LINUX
#  include <sys/socket.h>
#endif

// Focused stream fixture with independent per-client framing. The broad
// NiriMock protocol rewrite is separate; this fixture only speaks test cases.
class StreamServer
{
public:
    StreamServer()
    {
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                peers.append(socket);
                ++connections;
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(socket, &QLocalSocket::readyRead, &server, [this, socket, buffer] {
                    *buffer += socket->readAll();
                    for (;;) {
                        auto lf = buffer->indexOf('\n');
                        if (lf < 0)
                            break;
                        const auto line = buffer->left(lf);
                        buffer->remove(0, lf + 1);
                        requests.append(line);
                        if (line == "\"EventStream\"") {
                            ++subscriptions;
                            socket->write(initial);
                        } else {
                            socket->write("{\"Ok\":\"Handled\"}\n");
                        }
                    }
                });
                QObject::connect(socket, &QLocalSocket::disconnected, socket,
                                 &QObject::deleteLater);
            }
        });
    }
    bool listen() { return directory.isValid() && server.listen(directory.filePath("socket")); }
    QString path() const { return server.fullServerName(); }
    void write(const QByteArray &bytes)
    {
        for (auto socket : peers)
            if (socket && socket->state() == QLocalSocket::ConnectedState)
                socket->write(bytes);
    }
    void closePeers()
    {
        for (auto socket : peers)
            if (socket)
                socket->disconnectFromServer();
    }
    QByteArray initial = "{\"Ok\":\"Handled\"}\n";
    int connections = 0;
    int subscriptions = 0;
    QList<QByteArray> requests;
    QList<QPointer<QLocalSocket>> peers;
    QTemporaryDir directory;
    QLocalServer server;
};

static QByteArray snapshots()
{
    return "{\"WindowsChanged\":{\"windows\":[{\"id\":7,\"title\":\"fresh\",\"app_id\":\"test\","
           "\"is_focused\":true,\"is_floating\":false,\"is_urgent\":false,"
           "\"layout\":{\"tile_size\":[800,600],\"window_size\":[800,600],\"window_offset_in_"
           "tile\":[0,0]}}]}}\n"
           "{\"WorkspacesChanged\":{\"workspaces\":[{\"id\":9,\"idx\":1,\"name\":\"main\","
           "\"output\":\"DP-1\",\"is_active\":true,\"is_focused\":true,\"is_urgent\":false}]}}\n";
}

static int consumerProcess(const QString &kind)
{
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    QQmlComponent component(&engine);
    QByteArray body;
    if (kind == "state")
        body = "QtObject { property int observed: NiriState.windowCount; property string title: "
               "NiriState.focusedWindowTitle }";
    else if (kind == "windows")
        body = "NiriWindows {}";
    else if (kind == "workspaces")
        body = "NiriWorkspaces {}";
    else if (kind == "window")
        body = "NiriWindow { windowId: 7 }";
    else if (kind == "workspace")
        body = "NiriWorkspace { workspaceName: \"main\" }";
    else if (kind == "log")
        body = "NiriEventLog {}";
    else if (kind == "connection")
        body = "QtObject { property bool observed: NiriConnection.subscribed && "
               "NiriConnection.status === NiriConnection.Streaming; property int status: "
               "NiriConnection.status }";
    else if (kind == "error")
        body = "QtObject { property bool observed: !NiriConnection.subscribed && "
               "NiriConnection.status === NiriConnection.Failed && NiriConnection.error.code === "
               "\"ProtocolError\" && NiriConnection.errorString.length > 0; Component.onCompleted: "
               "NiriConnection.autoReconnect = false }";
    else if (kind == "events")
        body = "QtObject { id: root; property bool observed: false; property Connections listener: "
               "Connections { target: NiriEvents; function onWindowsChanged(windows) { "
               "root.observed = windows.length === 1 } } }";
    component.setData("import QtQml\nimport Niri 1.0\n" + body, QUrl());
    std::unique_ptr<QObject> object(component.create());
    if (!object) {
        qWarning() << component.errors();
        return 2;
    }
    QElapsedTimer timer;
    timer.start();
    bool observed = false;
    while (!observed && timer.elapsed() < 3000) {
        QCoreApplication::processEvents();
        if (auto *model = qobject_cast<QAbstractItemModel *>(object.get())) {
            observed = model->rowCount() > 0;
            if (observed && kind == "windows")
                observed = model->data(model->index(0, 0), NiriWindowsModel::TitleRole) == "fresh";
            if (observed && kind == "workspaces")
                observed = model->data(model->index(0, 0), NiriWorkspacesModel::NameRole) == "main";
        } else if (kind == "window") {
            observed = object->property("valid").toBool() && object->property("title") == "fresh";
        } else if (kind == "workspace") {
            observed = object->property("valid").toBool() && object->property("name") == "main";
        } else {
            observed = object->property("observed").toBool();
            if (kind == "state")
                observed = observed && object->property("title") == "fresh";
        }
        if (!observed)
            QThread::msleep(1);
    }
    return observed ? 0 : 3;
}

class TestSession : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        auto *session = NiriSession::instance();
        session->stop();
        session->setAutoReconnect(false);
        session->setConnectTimeoutMs(100);
        session->setHandshakeTimeoutMs(100);
        session->setMaxFrameBytes(NiriLineBuffer::DefaultMaxFrameBytes);
    }
    void cleanup() { NiriSession::instance()->stop(); }

    void freshQmlConsumer_data()
    {
        QTest::addColumn<QString>("kind");
        for (auto name : {"state", "windows", "workspaces", "window", "workspace", "log",
                          "connection", "events", "error"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void freshQmlConsumer()
    {
        QFETCH(QString, kind);
        StreamServer server;
        QVERIFY(server.listen());
        server.initial += snapshots();
        if (kind == "error")
            server.initial = "{}\n";
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("NIRI_SOCKET", server.path());
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {"--consumer", kind});
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
        QCOMPARE(server.subscriptions, 1);
        QCOMPARE(server.connections, 1);
    }

    void categorizedLogging_data()
    {
        QTest::addColumn<bool>("enabled");
        QTest::newRow("off") << false;
        QTest::newRow("on") << true;
    }
    void categorizedLogging()
    {
        QFETCH(bool, enabled);
        StreamServer server;
        QVERIFY(server.listen());
        server.initial += snapshots();
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("NIRI_SOCKET", server.path());
        environment.insert("QT_LOGGING_RULES",
                           enabled ? "niriqml.*=false;niriqml.events.debug=true"
                                   : "niriqml.*=false");
        // No controlling console in QProcess. Pin Qt's log sink to the channel
        // we inspect instead of relying on platform syslog/journal selection.
        environment.insert("QT_FORCE_STDERR_LOGGING", "1");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {"--consumer", "state"});
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5000);
        const auto diagnostic = child.readAllStandardError();
        QVERIFY2(child.exitCode() == 0, diagnostic.constData());
        QVERIFY2(diagnostic.contains("Event \"WindowsChanged\"") == enabled,
                 diagnostic.constData());
        QVERIFY(!diagnostic.contains("payload keys"));
        QVERIFY(!diagnostic.contains("app_id")); // Ordinary debug does not trace payloads.
    }

    void explicitStopSurvivesConsumers()
    {
        StreamServer server;
        QVERIFY(server.listen());
        auto *session = NiriSession::instance();
        session->selectEndpoint(server.path());
        session->stop();
        NiriWindowsModel windows;
        NiriWorkspacesModel workspaces;
        NiriWindowReactive window;
        NiriWorkspaceReactive workspace;
        NiriEventLog log;
        QVERIFY(NiriState::instance());
        QVERIFY(NiriEvents::instance());
        QVERIFY(NiriConnection::instance());
        QVERIFY(NiriRequests::instance());
        QVERIFY(NiriActions::instance());
        session->setAutoReconnect(true);
        QTest::qWait(150);
        QCOMPARE(server.connections, 0);
        QCOMPARE(session->status(), NiriSession::Stopped);
    }

    void endpointIndependentOfLiveness()
    {
        StreamServer environmentServer;
        StreamServer explicitServer;
        QVERIFY(environmentServer.listen());
        QVERIFY(explicitServer.listen());
        const auto previous = qgetenv("NIRI_SOCKET");
        const bool wasSet = qEnvironmentVariableIsSet("NIRI_SOCKET");
        auto restore = qScopeGuard([&] {
            if (wasSet)
                qputenv("NIRI_SOCKET", previous);
            else
                qunsetenv("NIRI_SOCKET");
        });
        qputenv("NIRI_SOCKET", environmentServer.path().toUtf8());
        auto *session = NiriSession::instance();
        session->selectEndpoint(explicitServer.path());
        explicitServer.initial.clear(); // B accepts sockets but does not acknowledge.
        session->start();
        QTRY_COMPARE(session->status(), NiriSession::Failed);
        QVERIFY(!session->isConnected());
        bool received = false;
        NiriRequests::instance()->send(
                QJsonObject{{"Action", QJsonObject{{"FocusWindow", QJsonObject{{"id", 7}}}}}},
                [&](bool ok, const QJsonObject &) { received = ok; });
        QTRY_VERIFY(received);
        QCOMPARE(environmentServer.connections, 0);
        QCOMPARE(explicitServer.connections, 2);
        QCOMPARE(session->currentEndpoint().source, NiriSession::Explicit);
        QCOMPARE(session->currentEndpoint().path, explicitServer.path());
        qunsetenv("NIRI_SOCKET");
        QCOMPARE(session->currentEndpoint().path, explicitServer.path());
    }

    void disconnectedEndpointNotifications()
    {
        auto *connection = NiriConnection::instance();
        connection->stop();
        QSignalSpy path(connection, &NiriConnection::socketPathChanged);
        auto *session = NiriSession::instance();
        const auto revision = session->currentEndpoint().revision;
        connection->selectEndpoint("first");
        connection->selectEndpoint("second");
        connection->selectEndpoint("");
        QCOMPARE(path.size(), 3);
        QCOMPARE(connection->socketPath(), QString());
        QCOMPARE(session->currentEndpoint().revision, revision + 3);
        QCOMPARE(session->currentEndpoint().source, NiriSession::Explicit);
    }

    void frameLimitSharedAcrossFacades()
    {
        QQmlEngine engine;
        auto *connection = NiriConnection::instance();
        auto *facade = NiriConnection::create(&engine, &engine);
        auto *session = NiriSession::instance();
        connection->setMaxFrameBytes(32);
        QCOMPARE(session->maxFrameBytes(), qsizetype(32));
        QCOMPARE(facade->maxFrameBytes(), qsizetype(32));
        facade->setMaxFrameBytes(-1);
        QCOMPARE(connection->maxFrameBytes(), qsizetype(0));
        session->setMaxFrameBytes(64);
        QCOMPARE(connection->maxFrameBytes(), qsizetype(64));
        QCOMPARE(facade->maxFrameBytes(), qsizetype(64));
    }

    void acknowledgment_data()
    {
        QTest::addColumn<QByteArray>("reply");
        QTest::addColumn<QString>("code");
        QTest::newRow("handled") << QByteArray(" { \"Ok\" : \"Handled\" } \n") << QString();
        QTest::newRow("rejected") << QByteArray("{\"Err\":\"no\"}\n")
                                  << QString("SubscriptionRejected");
        QTest::newRow("empty") << QByteArray("{}\n") << QString("ProtocolError");
        QTest::newRow("old-mock-shape")
                << QByteArray("{\"Ok\":{\"Handled\":null}}\n") << QString("ProtocolError");
        QTest::newRow("wrong-variant")
                << QByteArray("{\"Ok\":{\"Version\":\"26.04\"}}\n") << QString("ProtocolError");
        QTest::newRow("extra-key")
                << QByteArray("{\"Ok\":\"Handled\",\"extra\":1}\n") << QString("ProtocolError");
        QTest::newRow("event") << QByteArray("{\"WindowsChanged\":{\"windows\":[]}}\n")
                               << QString("ProtocolError");
        QTest::newRow("malformed") << QByteArray("{broken}\n") << QString("ProtocolError");
        QTest::newRow("none") << QByteArray() << QString("HandshakeTimeout");
        QTest::newRow("whitespace-is-not-ack")
                << QByteArray(" \t\r\n\n") << QString("HandshakeTimeout");
    }
    void acknowledgment()
    {
        QFETCH(QByteArray, reply);
        QFETCH(QString, code);
        StreamServer server;
        QVERIFY(server.listen());
        server.initial = reply;
        auto *session = NiriSession::instance();
        QSignalSpy events(session, &NiriSession::eventReceived);
        session->connectToSocket(server.path());
        if (code.isEmpty()) {
            QTRY_VERIFY(session->subscribed());
            QCOMPARE(session->status(), NiriSession::Streaming);
            QVERIFY(session->error().isEmpty());
            QTest::qWait(150); // Streaming may be idle beyond handshake deadline.
            QVERIFY(session->subscribed());
        } else {
            QTRY_COMPARE(session->status(), NiriSession::Failed);
            QVERIFY(!session->subscribed());
            QCOMPARE(session->error().value("code").toString(), code);
            QVERIFY(!session->errorString().isEmpty());
        }
        QCOMPARE(events.size(), 0);
    }

    void retryNotificationCancellation_data()
    {
        QTest::addColumn<bool>("stop");
        QTest::newRow("disconnect") << true;
        QTest::newRow("disable-policy") << false;
    }
    void retryNotificationCancellation()
    {
        QFETCH(bool, stop);
        StreamServer server;
        QVERIFY(server.listen());
        server.initial = "{}\n";
        auto *session = NiriSession::instance();
        session->setAutoReconnect(true);
        auto *connection = NiriConnection::instance();
        QSignalSpy retries(connection, &NiriConnection::reconnectScheduled);
        const auto handler =
                connect(connection, &NiriConnection::reconnectScheduled, connection, [=] {
                    if (stop)
                        connection->disconnect();
                    else
                        connection->setAutoReconnect(false);
                });
        connection->connectToSocket(server.path());
        QTRY_COMPARE(retries.size(), 1);
        QObject::disconnect(handler);
        QTest::qWait(600);
        QCOMPARE(server.connections, 1);
        QCOMPARE(session->status(), stop ? NiriSession::Stopped : NiriSession::Failed);
        server.initial = "{\"Ok\":\"Handled\"}\n";
        session->setAutoReconnect(true);
        if (stop) {
            QTest::qWait(600);
            QCOMPARE(server.connections, 1);
        } else {
            QTRY_VERIFY_WITH_TIMEOUT(session->subscribed(), 2000);
            QCOMPARE(server.connections, 2);
        }
    }

    void endpointSwitchDiscardsOldBatch()
    {
        StreamServer oldServer;
        StreamServer newServer;
        QVERIFY(oldServer.listen());
        QVERIFY(newServer.listen());
        oldServer.initial += "{\"First\":{}}\n{\"Stale\":{}}\n";
        newServer.initial += "{\"New\":{}}\n";
        auto *session = NiriSession::instance();
        QSignalSpy events(session, &NiriSession::eventReceived);
        const auto handler = connect(
                session, &NiriSession::eventReceived, session,
                [&](const QString &name, const QVariantMap &) {
                    if (name == "First") {
                        session->connectToSocket(newServer.path());
                        QCoreApplication::processEvents(); // Exercise nested drain ownership too.
                    }
                });
        session->connectToSocket(oldServer.path());
        QTRY_COMPARE(events.size(), 2);
        QObject::disconnect(handler);
        QCOMPARE(events[0][0].toString(), QString("First"));
        QCOMPARE(events[1][0].toString(), QString("New"));
        QCOMPARE(session->currentEndpoint().path, newServer.path());
        QVERIFY(session->subscribed());
    }

    void connectedHandlerStopSticks()
    {
        StreamServer server;
        QVERIFY(server.listen());
        auto *session = NiriSession::instance();
        const auto handler = connect(session, &NiriSession::connectedChanged, session, [&] {
            if (session->isConnected())
                session->stop();
        });
        session->connectToSocket(server.path());
        QTRY_COMPARE(server.connections, 1);
        QTRY_COMPARE(session->status(), NiriSession::Stopped);
        QObject::disconnect(handler);
        QVERIFY(!session->subscribed());
    }

    void selectionHandlerStopSticks()
    {
        StreamServer server;
        QVERIFY(server.listen());
        auto *connection = NiriConnection::instance();
        const auto handler = connect(connection, &NiriConnection::socketPathChanged, connection,
                                     [=] { connection->stop(); });
        connection->connectToSocket(server.path());
        QObject::disconnect(handler);
        QTest::qWait(150);
        QCOMPARE(server.connections, 0);
        QCOMPARE(connection->status(), NiriConnection::Stopped);
    }

    void oversizeReconnectsAndRetainsError()
    {
        StreamServer server;
        QVERIFY(server.listen());
        server.initial += QByteArray(100, 'x');
        auto *session = NiriSession::instance();
        session->setMaxFrameBytes(64);
        session->setAutoReconnect(true);
        QSignalSpy retries(session, &NiriSession::reconnectScheduled);
        session->connectToSocket(server.path());
        QTRY_COMPARE(session->status(), NiriSession::Backoff);
        QCOMPARE(session->error().value("code"), QVariant("FrameTooLarge"));
        QCOMPARE(retries.size(), 1);
        QCOMPARE(retries[0][1].toInt(), 1);
        server.initial = "{\"Ok\":\"Handled\"}\n";
        QTRY_VERIFY_WITH_TIMEOUT(session->subscribed(), 2000);
        QVERIFY(session->error().isEmpty());
#ifdef Q_OS_LINUX
        QVERIFY(session->peerInfo().credentialsAvailable);
        QCOMPARE(session->peerInfo().pid, QCoreApplication::applicationPid());
#endif
        server.closePeers();
        QTRY_COMPARE(retries.size(), 2);
        QCOMPARE(retries[1][1].toInt(), 1); // Reset only on accepted subscription.
    }

    void reentrantPolicyKeepsPropertyNotifications()
    {
        StreamServer server;
        QVERIFY(server.listen());
        auto *session = NiriSession::instance();
        session->setAutoReconnect(true);
        session->connectToSocket(server.path());
        QTRY_VERIFY(session->subscribed());
        QSignalSpy connected(session, &NiriSession::connectedChanged);
        QSignalSpy subscribed(session, &NiriSession::subscribedChanged);
        QSignalSpy errors(session, &NiriSession::errorChanged);
        const auto handler = connect(session, &NiriSession::statusChanged, session, [=] {
            if (session->status() == NiriSession::Backoff)
                session->setAutoReconnect(false);
        });
        server.closePeers();
        QTRY_COMPARE(session->status(), NiriSession::Failed);
        QObject::disconnect(handler);
        QCOMPARE(connected.size(), 1);
        QCOMPARE(subscribed.size(), 1);
        QCOMPARE(errors.size(), 1);
        QVERIFY(!session->errorString().isEmpty());
        QTest::qWait(600);
        QCOMPARE(server.connections, 1);
    }

    void reentrantEnableSchedulesOnce()
    {
        StreamServer server;
        QVERIFY(server.listen());
        server.initial = "{}\n";
        auto *session = NiriSession::instance();
        QSignalSpy retries(session, &NiriSession::reconnectScheduled);
        const auto handler = connect(session, &NiriSession::errorChanged, session, [=] {
            if (session->status() == NiriSession::Failed)
                session->setAutoReconnect(true);
        });
        session->connectToSocket(server.path());
        QTRY_COMPARE(session->status(), NiriSession::Backoff);
        QObject::disconnect(handler);
        QCOMPARE(retries.size(), 1);
    }

    void largeSnapshotsFragmented_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QJsonObject>("record");
        QTest::newRow("windows") << QString("WindowsChanged") << QString("windows")
                                 << QJsonObject{{"id", 1},
                                                {"title", QString(512, 'x')},
                                                {"app_id", "test"},
                                                {"is_focused", false},
                                                {"is_floating", false},
                                                {"is_urgent", false},
                                                {"layout",
                                                 QJsonObject{{"tile_size", QJsonArray{800, 600}},
                                                             {"window_size", QJsonArray{800, 600}},
                                                             {"window_offset_in_tile",
                                                              QJsonArray{0, 0}}}}};
        QTest::newRow("workspaces")
                << QString("WorkspacesChanged") << QString("workspaces")
                << QJsonObject{
                           {"id", 1},           {"idx", 1},           {"name", QString(512, 'w')},
                           {"output", "DP-1"},  {"is_active", false}, {"is_focused", false},
                           {"is_urgent", false}};
        QTest::newRow("casts")
                << QString("CastsChanged") << QString("casts")
                << QJsonObject{{"stream_id", 1},
                               {"session_id", 1},
                               {"kind", "PipeWire"},
                               {"target",
                                QJsonObject{{"Output", QJsonObject{{"name", QString(512, 'd')}}}}},
                               {"is_dynamic_target", false},
                               {"is_active", false}};
    }
    void largeSnapshotsFragmented()
    {
        QFETCH(QString, name);
        QFETCH(QString, key);
        QFETCH(QJsonObject, record);
        constexpr int count = 10000;
        QJsonArray records;
        for (int i = 0; i < count; ++i) {
            record[name == "CastsChanged" ? "stream_id" : "id"] = i + 1;
            records.append(record);
        }
        const QByteArray frame = QJsonDocument(QJsonObject{{name, QJsonObject{{key, records}}}})
                                         .toJson(QJsonDocument::Compact);
        QVERIFY(frame.size() < NiriLineBuffer::DefaultMaxFrameBytes);
        qInfo() << "Calibration" << name << count << "records:" << frame.size()
                << "bytes; default limit:" << NiriLineBuffer::DefaultMaxFrameBytes;
        StreamServer server;
        QVERIFY(server.listen());
        auto *session = NiriSession::instance();
        session->connectToSocket(server.path());
        QTRY_VERIFY(session->subscribed());
        bool observed = false;
        const auto handler =
                connect(session, &NiriSession::eventFrame, session,
                        [&](const NiriWire::Document &document, quint64 epoch) {
                            observed = epoch == session->epoch() && document.bytes == frame
                                    && document.root.lookup(name)->lookup(key)->arrayValue()->size()
                                            == count;
                        });
        // Deliberately fragment across non-record-aligned byte boundaries.
        for (qsizetype offset = 0; offset < frame.size(); offset += 4093) {
            server.write(frame.mid(offset, 4093));
            QCoreApplication::processEvents();
        }
        server.write("\n");
        QTRY_VERIFY_WITH_TIMEOUT(observed, 10000);
        QObject::disconnect(handler);
        QVERIFY(session->error().isEmpty());
    }

    void eof_data()
    {
        QTest::addColumn<QByteArray>("tail");
        QTest::addColumn<QString>("code");
        QTest::newRow("clean") << QByteArray(" \r\n") << QString("UnexpectedEof");
        QTest::newRow("truncated") << QByteArray("{\"Last\":{}}") << QString("TruncatedFrame");
    }
    void eof()
    {
        QFETCH(QByteArray, tail);
        QFETCH(QString, code);
        StreamServer server;
        QVERIFY(server.listen());
        auto *session = NiriSession::instance();
        QSignalSpy events(session, &NiriSession::eventReceived);
        session->connectToSocket(server.path());
        QTRY_VERIFY(session->subscribed());
        server.write("{\"Complete\":{}}\n" + tail);
        server.closePeers();
        QTRY_COMPARE(session->status(), NiriSession::Failed);
        QCOMPARE(events.size(), 1);
        QCOMPARE(session->error().value("code").toString(), code);
        QVERIFY(!session->peerInfo().credentialsAvailable);
    }

    void reducerRejectsViaProtocolError()
    {
        StreamServer server;
        QVERIFY(server.listen());
        server.initial += "{\"Impossible\":{}}\n{\"Stale\":{}}\n";
        auto *session = NiriSession::instance();
        QSignalSpy events(session, &NiriSession::eventReceived);
        const auto handler =
                connect(session, &NiriSession::eventFrame, session,
                        [=](const NiriWire::Document &document, quint64 epoch) {
                            session->protocolError(epoch, "Impossible sequence", document.bytes);
                        });
        session->connectToSocket(server.path());
        QTRY_COMPARE(session->status(), NiriSession::Failed);
        QObject::disconnect(handler);
        QCOMPARE(events.size(), 0);
        QCOMPARE(session->error().value("code").toString(), QString("ProtocolError"));
        QCOMPARE(session->error().value("raw").toByteArray(), QByteArray("{\"Impossible\":{}}"));
    }

    void engineLocalFacades()
    {
        auto *session = NiriSession::instance();
        QPointer<NiriConnection> released;
        {
            QQmlEngine first;
            QQmlEngine second;
            auto *a = NiriConnection::create(&first, &first);
            auto *b = NiriConnection::create(&second, &second);
            QVERIFY(a != b);
            QCOMPARE(a->parent(), &first);
            QCOMPARE(b->parent(), &second);
            released = a;
            QVERIFY(NiriEvents::create(&first, &first) != NiriEvents::create(&second, &second));
            QVERIFY(NiriState::create(&first, &first) != NiriState::create(&second, &second));
            QVERIFY(NiriRequests::create(&first, &first) != NiriRequests::create(&second, &second));
            QVERIFY(NiriActions::create(&first, &first) != NiriActions::create(&second, &second));
            a->selectEndpoint("engine-shared");
            QCOMPARE(b->socketPath(), QString("engine-shared"));
        }
        QVERIFY(released.isNull());
        QCOMPARE(NiriSession::instance(), session);
        QQmlEngine third;
        QCOMPARE(NiriConnection::create(&third, &third)->socketPath(), QString("engine-shared"));
    }

    void wrongThreadAcquisition()
    {
        bool rejected = false;
        auto *connection = NiriConnection::instance();
        auto *requests = NiriRequests::instance();
        auto *actions = NiriActions::instance();
        auto *state = NiriState::instance();
        const auto token = NiriSession::instance()->epoch();
        QThread *worker = QThread::create([&] {
            rejected = !NiriSession::instance() && !NiriConnection::instance()
                    && !NiriEvents::instance() && !NiriState::instance()
                    && !NiriRequests::instance() && !NiriActions::instance()
                    && !NiriConnection::create(nullptr, nullptr)
                    && !NiriEvents::create(nullptr, nullptr) && !NiriState::create(nullptr, nullptr)
                    && !NiriRequests::create(nullptr, nullptr)
                    && !NiriActions::create(nullptr, nullptr);
            connection->connectToSocket("wrong-thread");
            connection->setAutoReconnect(true);
            rejected = rejected && !connection->isConnected() && requests->version() == nullptr
                    && actions->focusWindow(7) == nullptr && state->windowCount() == 0;
        });
        for (int i = 0; i < 17; ++i)
            QTest::ignoreMessage(QtWarningMsg,
                                 "niriqml: acquisition/use requires the QCoreApplication thread");
        worker->start();
        QVERIFY(worker->wait(3000));
        delete worker;
        QVERIFY(rejected);
        QCOMPARE(NiriSession::instance()->epoch(), token);
    }

    void peerSecurity_data()
    {
        QTest::addColumn<QByteArray>("label");
        QTest::newRow("no-nul") << QByteArray("label");
        QTest::newRow("with-nul") << QByteArray("label\0", 6);
        QTest::newRow("raw-encoding") << QByteArray("\xfflabel", 6);
    }
    void peerSecurity()
    {
#ifdef Q_OS_LINUX
        QFETCH(QByteArray, label);
        int calls = 0;
        qintptr descriptorSeen = -1;
        auto info = NiriPeerInfoReader::read(
                42, [&](qintptr fd, int level, int option, void *value, unsigned int *size) {
                    descriptorSeen = fd;
                    Q_UNUSED(level)
                    if (option == SO_PEERCRED) {
                        errno = ENOPROTOOPT;
                        return -1;
                    }
                    ++calls;
                    if (calls == 1) {
                        *size = 300;
                        errno = ERANGE;
                        return -1;
                    }
                    if (calls == 2) {
                        *size = 400;
                        errno = ERANGE;
                        return -1;
                    }
                    std::memcpy(value, label.constData(), size_t(label.size()));
                    *size = static_cast<unsigned int>(label.size());
                    return 0;
                });
        QCOMPARE(calls, 3);
        QCOMPARE(descriptorSeen, qintptr(42));
        QVERIFY(!info.credentialsAvailable);
        QVERIFY(info.securityContextAvailable);
        if (label.endsWith('\0'))
            label.chop(1);
        QCOMPARE(info.peerSecurityContextBytes, label);
        QCOMPARE(info.peerSecurityContext, QString::fromUtf8(label));
#endif
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments()[1] == "--consumer")
        return consumerProcess(app.arguments()[2]);
    TestSession test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_session.moc"
