// Headless mock tests for NiriEvents typed dispatch.
//
// Injects synthetic events via NiriSession::eventReceived and verifies
// that NiriEvents dispatches the correct typed signals and updates its
// cached snapshots. No live niri socket needed.

#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QSignalSpy>
#include <QTest>

#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QScopeGuard>
#include "socketdirectory.h"
#include <memory>

#include "niriconnection.h"
#include "nirievents.h"
#include "niripendingreply.h"
#include "nirirequests.h"
#include "niritypes.h"
#include "niriworkspacereactive.h"
#include "ipcfixtures.h"
#include "streamfixture.h"

class TestEvents : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(NiriEvents::instance());
        QVERIFY(NiriConnection::instance());
    }
    void cleanup() { m_stream.reset(); }

    void windowsChangedDispatches()
    {
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::windowsChanged);

        QJsonArray windows;
        QJsonObject w = windowFixture();
        w["id"] = 1;
        w["title"] = "test";
        w["app_id"] = QStringLiteral("test.app");
        w["is_focused"] = true;
        w["is_floating"] = false;
        windows.append(w);
        emitEvent("WindowsChanged", {{"windows", windows.toVariantList()}});

        QCOMPARE(spy.count(), 1);
        QVariantList result = spy.at(0).at(0).toList();
        QCOMPARE(result.size(), 1);
        NiriWindow rw = result.at(0).value<NiriWindow>();
        QCOMPARE(rw.id, quint64(1));
        QCOMPARE(rw.title, QStringLiteral("test"));
        QVERIFY(rw.isFocused);
    }

    void windowsChangedUpdatesSnapshot()
    {
        QJsonArray windows;
        QJsonObject w = windowFixture();
        w["id"] = 42;
        w["title"] = "snap";
        windows.append(w);
        emitEvent("WindowsChanged", {{"windows", windows.toVariantList()}});

        QVariantList snap = NiriEvents::instance()->lastWindowsSnapshot();
        QCOMPARE(snap.size(), 1);
        QCOMPARE(snap.at(0).value<NiriWindow>().id, quint64(42));
    }

    void workspaceActivatedDispatches()
    {
        auto workspace = workspaceFixture();
        workspace["id"] = 3;
        emitEvent("WorkspacesChanged", {{"workspaces", QJsonArray{workspace}.toVariantList()}});
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::workspaceActivated);

        emitEvent("WorkspaceActivated", {{"id", 3}, {"focused", true}});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).metaType(), QMetaType::fromType<QString>());
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("3"));
        QVERIFY(spy.at(0).at(1).toBool());
    }

    void windowFocusChangedUpdatesCache()
    {
        // Set up a window in the snapshot first
        QJsonArray windows;
        QJsonObject w = windowFixture();
        w["id"] = 99;
        w["is_focused"] = false;
        windows.append(w);
        emitEvent("WindowsChanged", {{"windows", windows.toVariantList()}});

        // Now focus it
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::windowFocusChanged);
        emitEvent("WindowFocusChanged", {{"id", 99}});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).metaType(), QMetaType::fromType<QString>());
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("99"));
        QCOMPARE(NiriEvents::instance()->lastFocusedWindowId(), quint64(99));

        // Cache updated
        NiriWindow snap = NiriEvents::instance()->lastWindowsSnapshot().at(0).value<NiriWindow>();
        QVERIFY(snap.isFocused);
    }

    void keyboardLayoutsPopulated()
    {
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::keyboardLayoutsChanged);

        QJsonObject kl;
        kl["names"] = QJsonArray{QStringLiteral("us"), QStringLiteral("fr")};
        kl["current_idx"] = 0;
        emitEvent("KeyboardLayoutsChanged", {{"keyboard_layouts", kl}});

        QCOMPARE(spy.count(), 1);
        QStringList expected = {QStringLiteral("us"), QStringLiteral("fr")};
        QVERIFY(NiriEvents::instance()->lastKeyboardLayouts());
        QCOMPARE(NiriEvents::instance()->lastKeyboardLayouts()->names, expected);
    }

    void overviewOpenedOrClosed()
    {
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::overviewOpenedOrClosed);

        emitEvent("OverviewOpenedOrClosed", {{"is_open", true}});

        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(0).toBool());
        QVERIFY(NiriEvents::instance()->lastOverviewOpen());
    }

    void unknownEventFiresRawOnly()
    {
        QSignalSpy rawSpy(NiriEvents::instance(), &NiriEvents::rawEvent);
        QSignalSpy windowsSpy(NiriEvents::instance(), &NiriEvents::windowsChanged);

        emitEvent("SomeUnknownFutureEvent", {{"magic", 42}});

        QCOMPARE(rawSpy.count(), 1);
        QCOMPARE(rawSpy.at(0).at(0).toString(), QStringLiteral("SomeUnknownFutureEvent"));
        // windowsChanged should NOT fire for unknown events
        QCOMPARE(windowsSpy.count(), 0);
    }

    void multipleEventsAccumulateSnapshot()
    {
        // Window appears
        QJsonArray windows;
        QJsonObject w1 = windowFixture(), w2 = windowFixture();
        w1["id"] = 1;
        w2["id"] = 2;
        windows.append(w1);
        windows.append(w2);
        emitEvent("WindowsChanged", {{"windows", windows.toVariantList()}});

        // Window closes
        emitEvent("WindowClosed", {{"id", 1}});

        QVariantList snap = NiriEvents::instance()->lastWindowsSnapshot();
        QCOMPARE(snap.size(), 1);
        QCOMPARE(snap.at(0).value<NiriWindow>().id, quint64(2));
    }

    void socketTimeoutFailsAfterDelay_data()
    {
        QTest::addColumn<bool>("finishBeforeVerification");
        QTest::newRow("ordinary") << false;
        QTest::newRow("completed-during-verification") << true;
    }

    void socketTimeoutFailsAfterDelay()
    {
        QFETCH(bool, finishBeforeVerification);
        SocketDirectory socketDir;
        QVERIFY(socketDir.isValid());
        QLocalServer hangServer;
        QVERIFY(hangServer.listen(socketDir.filePath(QStringLiteral("socket"))));
        const bool socketWasSet = qEnvironmentVariableIsSet("NIRI_SOCKET");
        const QByteArray oldSock = qgetenv("NIRI_SOCKET");
        auto *requests = NiriRequests::instance();
        const int oldTimeout = requests->requestTimeoutMs();
        const auto restore = qScopeGuard([&]() {
            requests->setRequestTimeoutMs(oldTimeout);
            if (socketWasSet)
                qputenv("NIRI_SOCKET", oldSock);
            else
                qunsetenv("NIRI_SOCKET");
            NiriConnection::instance()->useEnvironmentEndpoint();
        });
        qputenv("NIRI_SOCKET", hangServer.fullServerName().toUtf8());
        // Disconnect preserves endpoint selection now. Refresh Environment
        // deliberately; a request must never infer selection from liveness.
        NiriConnection::instance()->disconnect();
        NiriConnection::instance()->useEnvironmentEndpoint();

        requests->setRequestTimeoutMs(100);
        QElapsedTimer elapsed;
        elapsed.start();
        NiriPendingReply *reply = requests->version();
        const std::unique_ptr<NiriPendingReply> retained(reply);
        int completions = 0;
        bool failed = false;
        QString error;
        qint64 finishedAfterMs = -1;
        QObject observer;
        connect(reply, &NiriPendingReply::finished, &observer, [&]() {
            ++completions;
            finishedAfterMs = elapsed.elapsed();
            failed = reply->isError();
            error = reply->error().message;
        });

        if (finishBeforeVerification)
            QTest::qWait(150);

        // Prove this is a connected peer withholding a reply, not a bad endpoint.
        QTRY_VERIFY_WITH_TIMEOUT(hangServer.hasPendingConnections(), 500);
        QLocalSocket *peer = hangServer.nextPendingConnection();
        QVERIFY(peer);
        QTRY_VERIFY_WITH_TIMEOUT(peer->canReadLine(), 500);
        QCOMPARE(peer->readLine(), QByteArray("\"Version\"\n"));
        QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 500);
        QVERIFY(finishedAfterMs >= 90); // Qt's coarse timer may fire up to 5% early.
        QVERIFY(failed);
        QCOMPARE(error, QStringLiteral("Timeout"));
    }

    void oversizedStreamReconnects_data()
    {
        QTest::addColumn<bool>("terminated");
        QTest::newRow("complete-line") << true;
        QTest::newRow("unterminated") << false;
    }

    void oversizedStreamReconnects()
    {
        QFETCH(bool, terminated);
        SocketDirectory socketDir;
        QVERIFY(socketDir.isValid());
        QLocalServer server;
        QVERIFY(server.listen(socketDir.filePath(QStringLiteral("socket"))));
        auto *connection = NiriConnection::instance();
        const auto oldLimit = connection->maxFrameBytes();
        const auto oldPolicy = connection->autoReconnect();
        const auto restore = qScopeGuard([&]() {
            connection->disconnect();
            connection->setMaxFrameBytes(oldLimit);
            connection->setAutoReconnect(oldPolicy);
        });
        connection->disconnect();
        connection->setAutoReconnect(true);
        connection->setMaxFrameBytes(32);
        QSignalSpy retry(connection, &NiriConnection::reconnectScheduled);
        QSignalSpy events(connection, &NiriConnection::eventReceived);
        QString observedError;
        const auto capture = connect(connection, &NiriConnection::connectedChanged, this, [&]() {
            if (!connection->isConnected())
                observedError = connection->errorString();
        });
        const auto detach = qScopeGuard([&]() { QObject::disconnect(capture); });
        connection->connectToSocket(server.fullServerName());
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 500);
        auto *peer = server.nextPendingConnection();
        QVERIFY(peer);
        QTRY_VERIFY_WITH_TIMEOUT(peer->canReadLine(), 500);
        QCOMPARE(peer->readLine(), QByteArray("\"EventStream\"\n"));
        peer->write(QByteArray(33, 'x') + (terminated ? "\n" : ""));
        peer->flush();
        QTRY_COMPARE_WITH_TIMEOUT(retry.count(), 1, 500);
        QVERIFY2(observedError.contains(QStringLiteral("Framing error")),
                 qPrintable(observedError));
        QCOMPARE(connection->errorString(), observedError);
        QVERIFY(!connection->isConnected());
        QTRY_COMPARE_WITH_TIMEOUT(peer->state(), QLocalSocket::UnconnectedState, 500);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1500);
        auto *replacement = server.nextPendingConnection();
        QVERIFY(replacement);
        QTRY_VERIFY_WITH_TIMEOUT(replacement->canReadLine(), 500);
        QCOMPARE(replacement->readLine(), QByteArray("\"EventStream\"\n"));
        replacement->write("{\"Ok\":\"Handled\"}\n{\"Recovered\":{}}\n");
        replacement->flush();
        QTRY_COMPARE_WITH_TIMEOUT(events.count(), 1, 500);
        QCOMPARE(events.at(0).at(0).toString(), QStringLiteral("Recovered"));
    }

    void completeEventsPrecedeFramingFailure_data()
    {
        QTest::addColumn<bool>("combined");
        QTest::newRow("one-read") << true;
        QTest::newRow("separate-reads") << false;
    }

    void completeEventsPrecedeFramingFailure()
    {
        QFETCH(bool, combined);
        SocketDirectory socketDir;
        QVERIFY(socketDir.isValid());
        QLocalServer server;
        QVERIFY(server.listen(socketDir.filePath(QStringLiteral("socket"))));
        auto *connection = NiriConnection::instance();
        const auto oldLimit = connection->maxFrameBytes();
        const auto oldPolicy = connection->autoReconnect();
        const auto restore = qScopeGuard([&]() {
            connection->stop();
            connection->setMaxFrameBytes(oldLimit);
            connection->setAutoReconnect(oldPolicy);
        });
        connection->stop();
        connection->setMaxFrameBytes(32);
        connection->setAutoReconnect(false);
        connection->connectToSocket(server.fullServerName());
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 500);
        auto *peer = server.nextPendingConnection();
        QVERIFY(peer);
        QTRY_VERIFY_WITH_TIMEOUT(peer->canReadLine(), 500);
        QCOMPARE(peer->readLine(), QByteArray("\"EventStream\"\n"));
        peer->write("{\"Ok\":\"Handled\"}\n");
        peer->flush();
        QTRY_VERIFY_WITH_TIMEOUT(connection->subscribed(), 500);
        QSignalSpy events(connection, &NiriConnection::eventReceived);
        QSignalSpy focused(NiriEvents::instance(), &NiriEvents::windowFocusChanged);
        const QByteArray complete =
                "{\"WindowFocusChanged\":{\"id\":1}}\n{\"WindowFocusChanged\":{\"id\":2}}\n";
        const QByteArray oversized(33, 'x');
        if (combined) {
            // One write/flush with no event-loop turn between the valid prefix and tail.
            peer->write(complete + oversized);
        } else {
            peer->write(complete);
            peer->flush();
            QTRY_COMPARE_WITH_TIMEOUT(events.count(), 2, 500);
            peer->write(oversized);
        }
        peer->flush();
        QTRY_COMPARE_WITH_TIMEOUT(connection->status(), NiriConnection::Failed, 500);
        QCOMPARE(connection->error().value("code").toString(), QStringLiteral("FrameTooLarge"));
        QVERIFY(connection->errorString().contains(QStringLiteral("Framing error")));
        QVERIFY(!connection->isConnected());
        QTRY_COMPARE_WITH_TIMEOUT(peer->state(), QLocalSocket::UnconnectedState, 500);
        QCOMPARE(events.count(), 2);
        QCOMPARE(focused.count(), 2);
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(events.at(i).at(0).toString(), QStringLiteral("WindowFocusChanged"));
            QCOMPARE(events.at(i).at(1).toMap().value("id").toULongLong(), quint64(i + 1));
            QCOMPARE(focused.at(i).at(0).metaType(), QMetaType::fromType<QString>());
            QCOMPARE(focused.at(i).at(0).toString(), QString::number(i + 1));
        }
    }

    void completeReplyPrecedesFramingFailure_data() { oversizedStreamReconnects_data(); }

    void completeReplyPrecedesFramingFailure()
    {
        QFETCH(bool, terminated);
        SocketDirectory socketDir;
        QVERIFY(socketDir.isValid());
        QLocalServer server;
        QVERIFY(server.listen(socketDir.filePath(QStringLiteral("socket"))));
        auto *requests = NiriRequests::instance();
        auto *connection = NiriConnection::instance();
        const auto oldEndpoint = NiriSession::instance()->currentEndpoint();
        const auto oldLimit = requests->maxFrameBytes();
        const auto restore = qScopeGuard([&]() {
            requests->setMaxFrameBytes(oldLimit);
            if (oldEndpoint.source == NiriSession::Environment)
                connection->useEnvironmentEndpoint();
            else
                connection->selectEndpoint(oldEndpoint.path);
        });
        connection->stop();
        connection->selectEndpoint(server.fullServerName());
        requests->setMaxFrameBytes(32);
        auto *reply = requests->sendRaw(QStringLiteral("Version"));
        const std::unique_ptr<NiriPendingReply> retained(reply);
        int completions = 0;
        bool failed = true;
        QVariant value;
        QString error;
        QObject observer;
        connect(reply, &NiriPendingReply::finished, &observer, [&]() {
            ++completions;
            failed = reply->isError();
            value = reply->value();
            error = reply->error().message;
        });
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 500);
        auto *peer = server.nextPendingConnection();
        QVERIFY(peer);
        QTRY_VERIFY_WITH_TIMEOUT(peer->canReadLine(), 500);
        QCOMPARE(peer->readLine(), QByteArray("\"Version\"\n"));
        peer->write("{\"Ok\":{}}\n" + QByteArray(33, 'x') + (terminated ? "\n" : ""));
        peer->flush();
        QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 500);
        QVERIFY2(!failed, qPrintable(error));
        QCOMPARE(value.metaType(), QMetaType::fromType<QVariantMap>());
        QVERIFY(value.toMap().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(peer->state(), QLocalSocket::UnconnectedState, 500);
    }

    void oversizedReplyFails_data() { oversizedStreamReconnects_data(); }

    void oversizedReplyFails()
    {
        QFETCH(bool, terminated);
        SocketDirectory socketDir;
        QVERIFY(socketDir.isValid());
        QLocalServer server;
        QVERIFY(server.listen(socketDir.filePath(QStringLiteral("socket"))));
        auto *requests = NiriRequests::instance();
        const auto oldLimit = requests->maxFrameBytes();
        const auto oldTimeout = requests->requestTimeoutMs();
        const bool socketWasSet = qEnvironmentVariableIsSet("NIRI_SOCKET");
        const auto oldSocket = qgetenv("NIRI_SOCKET");
        const auto restore = qScopeGuard([&]() {
            requests->setMaxFrameBytes(oldLimit);
            requests->setRequestTimeoutMs(oldTimeout);
            if (socketWasSet)
                qputenv("NIRI_SOCKET", oldSocket);
            else
                qunsetenv("NIRI_SOCKET");
            NiriConnection::instance()->useEnvironmentEndpoint();
        });
        NiriConnection::instance()->disconnect();
        qputenv("NIRI_SOCKET", server.fullServerName().toUtf8());
        NiriConnection::instance()->useEnvironmentEndpoint();
        requests->setMaxFrameBytes(32);
        requests->setRequestTimeoutMs(2000);
        auto *reply = requests->version();
        const std::unique_ptr<NiriPendingReply> retained(reply);
        int completions = 0;
        bool failed = false;
        QString error;
        QObject observer;
        connect(reply, &NiriPendingReply::finished, &observer, [&]() {
            ++completions;
            failed = reply->isError();
            error = reply->error().message;
        });
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 500);
        auto *peer = server.nextPendingConnection();
        QVERIFY(peer);
        QTRY_VERIFY_WITH_TIMEOUT(peer->canReadLine(), 500);
        QCOMPARE(peer->readLine(), QByteArray("\"Version\"\n"));
        peer->write("{\"Ok\":{\"Version\":\"" + QByteArray(33, 'x') + "\"}}"
                    + (terminated ? "\n" : ""));
        peer->flush();
        QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 500);
        QVERIFY(failed);
        QVERIFY2(error.contains(QStringLiteral("Framing error")), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(peer->state(), QLocalSocket::UnconnectedState, 500);
    }

    void pendingReplyRetainedAfterFinished()
    {
        NiriSession::instance()->selectEndpoint(QString());
        NiriPendingReply *reply = NiriRequests::instance()->version();
        QPointer<NiriPendingReply> guard(reply);
        QVERIFY(!guard.isNull());
        QTRY_VERIFY(reply->completed());
        QTest::qWait(50);
        QVERIFY(!guard.isNull());
        QVERIFY(reply->isError());
        delete reply;
        QVERIFY(guard.isNull());
    }

    void clearFocusOnNullId()
    {
        // Bootstrap two windows — id=1 focused, id=2 not
        QJsonArray windows;
        QJsonObject w1 = windowFixture(), w2 = windowFixture();
        w1["id"] = 1;
        w1["title"] = QStringLiteral("focused");
        w1["is_focused"] = true;
        w2["id"] = 2;
        w2["title"] = QStringLiteral("unfocused");
        w2["is_focused"] = false;
        windows.append(w1);
        windows.append(w2);
        emitEvent("WindowsChanged", {{"windows", windows.toVariantList()}});

        QCOMPARE(NiriEvents::instance()->lastFocusedWindowId(), quint64(1));

        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::windowFocusChanged);

        // niri sends WindowFocusChanged { id: null }, distinct from ID zero,
        // when focus clears — e.g. switching to an empty workspace.
        emitEvent("WindowFocusChanged", {{"id", QVariant::fromValue(nullptr)}});

        QVERIFY(!NiriEvents::instance()->lastFocusedWindowId());
        QCOMPARE(spy.at(0).at(0).metaType(), QMetaType::fromType<std::nullptr_t>());
        QCOMPARE(spy.count(), 1);
        for (const QVariant &v : NiriEvents::instance()->lastWindowsSnapshot()) {
            NiriWindow w = v.value<NiriWindow>();
            QVERIFY(!w.isFocused);
        }
    }

    void workspaceReactiveHandlesActivation()
    {
        // Bootstrap: send WorkspacesChanged with two workspaces
        QJsonArray wss;
        QJsonObject ws1 = workspaceFixture(), ws2 = workspaceFixture();
        ws1["id"] = 1;
        ws1["output"] = QStringLiteral("eDP-1");
        ws1["is_active"] = true;
        ws2["id"] = 2;
        ws2["output"] = QStringLiteral("eDP-1");
        ws2["is_active"] = false;
        wss.append(ws1);
        wss.append(ws2);
        QJsonObject payload;
        payload["workspaces"] = wss;
        emitEvent(QStringLiteral("WorkspacesChanged"),
                  {{QStringLiteral("workspaces"), wss.toVariantList()}});

        NiriWorkspaceReactive reactive;
        reactive.setWorkspaceId(2);
        QVERIFY(reactive.valid());
        QVERIFY(!reactive.isActive());

        // Fire WorkspaceActivated — reads target->output after loop exit.
        // On buggy code: dangling stack pointer under ASan, garbage otherwise.
        emitEvent(QStringLiteral("WorkspaceActivated"),
                  {{QStringLiteral("id"), 2}, {QStringLiteral("focused"), true}});

        QVERIFY(reactive.isActive());
    }

private:
    std::unique_ptr<StateStreamFixture> m_stream;
    void emitEvent(const QString &name, const QVariantMap &payload)
    {
        if (!m_stream || !NiriSession::instance()->subscribed()
            || NiriSession::instance()->currentEndpoint().path
                    != m_stream->server.fullServerName()) {
            m_stream = std::make_unique<StateStreamFixture>();
            QVERIFY(m_stream->start());
        }
        QSignalSpy raw(NiriEvents::instance(), &NiriEvents::rawEvent);
        const auto value = NiriWire::fromQVariantLossy(payload);
        QVERIFY(value);
        m_stream->send(*NiriWire::encode(*NiriWire::WireValue::object({{name, *value}})));
        QTRY_COMPARE(raw.size(), 1);
    }
};

QTEST_MAIN(TestEvents)
#include "test_events.moc"
