#include "niriactions.h"
#include "niriconnection.h"
#include "nirievents.h"
#include "nirirequests.h"
#include "niristate.h"
#include "niriworkspacereactive.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <memory>

class TestSessionReview : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        NiriSession::instance()->stop();
        NiriSession::instance()->setAutoReconnect(false);
    }

    void pendingFacadeReleasesCallback()
    {
        QTemporaryDir directory;
        QLocalServer server;
        QVERIFY(server.listen(directory.filePath("socket")));
        NiriSession::instance()->selectEndpoint(server.fullServerName());
        auto engine = std::make_unique<QQmlEngine>();
        auto *requests = NiriRequests::create(engine.get(), engine.get());
        auto captured = std::make_shared<int>(42);
        std::weak_ptr<int> released = captured;
        int completions = 0;
        requests->sendJson("Version",
                           [captured, &completions](bool, const auto &) { ++completions; });
        captured.reset();
        QTRY_VERIFY(server.hasPendingConnections());
        auto *peer = server.nextPendingConnection();
        QTRY_VERIFY(peer->canReadLine());
        engine.reset();
        QVERIFY(released.expired());
        QTRY_COMPARE(peer->state(), QLocalSocket::UnconnectedState);
        QTest::qWait(30);
        QCOMPARE(completions, 0);
    }

    void actionEngineDestruction_data()
    {
        QTest::addColumn<bool>("replyArrives");
        QTest::newRow("reply") << true;
        QTest::newRow("timeout") << false;
    }

    void actionEngineDestruction()
    {
        QFETCH(bool, replyArrives);
        QTemporaryDir directory;
        QLocalServer server;
        QVERIFY(server.listen(directory.filePath("socket")));
        NiriSession::instance()->selectEndpoint(server.fullServerName());
        auto *requests = NiriRequests::instance();
        requests->setRequestTimeoutMs(100);
        auto engine = std::make_unique<QQmlEngine>();
        auto *actions = NiriActions::create(engine.get(), engine.get());
        QPointer<NiriPendingReply> reply = actions->spawn({"true"});
        std::unique_ptr<NiriPendingReply> retained(reply);
        QTRY_VERIFY(server.hasPendingConnections());
        auto *peer = server.nextPendingConnection();
        QTRY_VERIFY(peer->canReadLine());
        engine.reset();
        // E5 replies are unparented and retainable. Engine teardown cancels
        // their operation instead of deleting a caller-held reply.
        QVERIFY(reply);
        if (replyArrives && peer->state() == QLocalSocket::ConnectedState)
            peer->write("{\"Ok\":\"Handled\"}\n");
        QTest::qWait(150);
        QVERIFY(reply->completed());
        QCOMPARE(reply->error().category, QString("Cancelled"));
        QTRY_COMPARE(peer->state(), QLocalSocket::UnconnectedState);
        requests->setRequestTimeoutMs(5000);
    }

    void staleEventRejected_data()
    {
        QTest::addColumn<bool>("switchEndpoint");
        QTest::newRow("stop") << false;
        QTest::newRow("endpoint") << true;
    }

    void staleEventRejected()
    {
        QFETCH(bool, switchEndpoint);
        QTemporaryDir directory;
        QLocalServer server;
        QVERIFY(server.listen(directory.filePath("socket")));
        auto *connection = NiriConnection::instance();
        QObject observer;
        // This connection deliberately precedes the events facade's session
        // receiver. Re-entering here must not relabel old data with a new epoch.
        connect(connection, &NiriConnection::eventReceived, &observer,
                [&](const QString &, const QVariantMap &) {
                    if (switchEndpoint)
                        NiriSession::instance()->selectEndpoint(directory.filePath("other"));
                    else
                        NiriSession::instance()->stop();
                });
        auto *events = NiriEvents::instance();
        QSignalSpy raw(events, &NiriEvents::rawEvent);
        QSignalSpy typed(events, &NiriEvents::windowClosed);
        connection->connectToSocket(server.fullServerName());
        QTRY_VERIFY(server.hasPendingConnections());
        auto *peer = server.nextPendingConnection();
        QTRY_VERIFY(peer->canReadLine());
        peer->write("{\"Ok\":\"Handled\"}\n{\"WindowClosed\":{\"id\":7}}\n");
        QTRY_VERIFY(!connection->isConnected());
        QTest::qWait(20);
        QCOMPARE(typed.count(), 0);
        QCOMPARE(raw.count(), 0);
    }

    void lateWorkspaceState()
    {
        auto *events = NiriEvents::instance();
        events->dispatchEvent("WorkspacesChanged",
                              {{"workspaces",
                                QVariantList{QVariantMap{{"id", 1},
                                                         {"idx", 1},
                                                         {"is_urgent", false},
                                                         {"output", "DP-1"},
                                                         {"is_active", true},
                                                         {"is_focused", true}},
                                             QVariantMap{{"id", 2},
                                                         {"idx", 2},
                                                         {"is_urgent", false},
                                                         {"output", "DP-1"},
                                                         {"is_active", false},
                                                         {"is_focused", false}}}}});
        QQmlEngine firstEngine;
        auto *first = NiriState::create(&firstEngine, &firstEngine);
        QCOMPARE(first->focusedWorkspaceId(), quint64(1));
        events->dispatchEvent("WorkspaceActivated", {{"id", 2}, {"focused", true}});
        events->dispatchEvent("WorkspaceActiveWindowChanged",
                              {{"workspace_id", 2}, {"active_window_id", 9}});
        QCOMPARE(first->focusedWorkspaceId(), quint64(2));
        QQmlEngine secondEngine;
        auto *second = NiriState::create(&secondEngine, &secondEngine);
        QCOMPARE(second->focusedWorkspaceId(), quint64(2));
        QCOMPARE(second->activeOutput(), QString("DP-1"));
        NiriWorkspaceReactive workspace;
        workspace.setWorkspaceId(2);
        QVERIFY(workspace.isActive());
        QVERIFY(workspace.isFocused());
        QCOMPARE(workspace.activeWindowId(), quint64(9));
    }

    void urgentThreadGuard()
    {
        NiriWorkspaceReactive workspace;
        NiriEvents::instance()->dispatchEvent("WorkspacesChanged",
                                              {{"workspaces",
                                                QVariantList{QVariantMap{{"id", 1},
                                                                         {"idx", 1},
                                                                         {"is_urgent", true},
                                                                         {"is_active", false},
                                                                         {"is_focused", false}}}}});
        workspace.setWorkspaceId(1);
        QVERIFY(workspace.isUrgent());
        bool urgent = true;
        QTest::ignoreMessage(QtWarningMsg,
                             "niriqml: acquisition/use requires the QCoreApplication thread");
        std::unique_ptr<QThread> worker(QThread::create([&] { urgent = workspace.isUrgent(); }));
        worker->start();
        QVERIFY(worker->wait(1000));
        QVERIFY(!urgent);
    }
};

QTEST_MAIN(TestSessionReview)
#include "test_session_review.moc"
