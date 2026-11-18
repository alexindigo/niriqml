#include "niristate.h"
#include "nirieventlog.h"
#include "niriwindowreactive.h"
#include "niriworkspacereactive.h"
#include "niriwindowsmodel.h"
#include "niriworkspacesmodel.h"
#include "generated/niriipccodec_generated.h"
#include "streamfixture.h"
#include <QAbstractItemModelTester>
#include <QEventLoop>
#include <QMetaProperty>
#include <QSignalSpy>
#include <memory>

namespace {
QVariantMap observable(QObject *object)
{
    QVariantMap values;
    const auto *meta = object->metaObject();
    for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
        const auto property = meta->property(i);
        values.insert(property.name(), property.read(object));
    }
    return values;
}
bool send(StateStreamFixture &stream, const NiriEvent &event)
{
    auto old = NiriSession::instance()->state().revision();
    stream.send(*NiriWire::encode(*NiriIpc::encode(event)));
    return QTest::qWaitFor([old] { return NiriSession::instance()->state().revision() > old; });
}
bool populate(StateStreamFixture &stream)
{
    NiriWindow window;
    window.id = 7;
    window.title = "title";
    window.appId = "app";
    window.isFocused = true;
    NiriWorkspace workspace;
    workspace.id = 8;
    workspace.name = "workspace";
    workspace.output = "output";
    workspace.isActive = workspace.isFocused = true;
    NiriCast cast;
    cast.streamId = 9;
    cast.kind = NiriCastKind(NiriCastKind::PipeWire);
    cast.target = NiriCastTarget::fromNothing({});
    return send(stream, NiriEvent::fromWorkspacesChanged({{workspace}, {}}))
            && send(stream, NiriEvent::fromWindowsChanged({{window}, {}}))
            && send(stream, NiriEvent::fromKeyboardLayoutsChanged({{0, {"us"}, {}}, {}}))
            && send(stream, NiriEvent::fromOverviewOpenedOrClosed({true, {}}))
            && send(stream, NiriEvent::fromConfigLoaded({true, {}}))
            && send(stream, NiriEvent::fromCastsChanged({{cast}, {}}));
}
} // namespace

class TestProjectionReset : public QObject
{
    Q_OBJECT
private slots:
    void init() { NiriSession::instance()->stop(); }
    void cleanup() { NiriSession::instance()->stop(); }
    void nestedResetBurst_data()
    {
        QTest::addColumn<QString>("cause");
        QTest::addColumn<bool>("summary");
        for (const auto &cause :
             QStringList{"Connecting", "EndpointChanged", "Stopped", "ProtocolError", "event"}) {
            QTest::newRow(qPrintable(cause + "-rows")) << cause << false;
            QTest::newRow(qPrintable(cause + "-summary")) << cause << true;
        }
    }
    void nestedResetBurst()
    {
        QFETCH(QString, cause);
        QFETCH(bool, summary);
        StateStreamFixture stream;
        QVERIFY(stream.start());
        QVERIFY(populate(stream));
        auto *session = NiriSession::instance();
        auto *state = NiriState::instance();
        NiriWindowsModel windows;
        NiriWorkspacesModel workspaces;
        QAbstractItemModelTester wt(&windows,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester wst(&workspaces,
                                     QAbstractItemModelTester::FailureReportingMode::QtTest);
        NiriWindowReactive window;
        window.setWindowId(17);
        NiriWorkspaceReactive workspace;
        workspace.setWorkspaceId(18);

        NiriWindow record;
        record.id = 17;
        record.title = "new";
        record.isFocused = true;
        NiriWorkspace wr;
        wr.id = 18;
        wr.name = "new-workspace";
        wr.isActive = wr.isFocused = true;
        const QList<NiriEvent> burst{NiriEvent::fromWorkspacesChanged({{wr}, {}}),
                                     NiriEvent::fromWindowsChanged({{record}, {}}),
                                     NiriEvent::fromWindowUrgencyChanged({17, true, {}}),
                                     NiriEvent::fromKeyboardLayoutsChanged({{0, {"de"}, {}}, {}}),
                                     NiriEvent::fromOverviewOpenedOrClosed({true, {}}),
                                     NiriEvent::fromConfigLoaded({true, {}}),
                                     NiriEvent::fromCastsChanged({{}, {}})};
        QList<QByteArray> expected;
        QByteArray bytes = "{\"Ok\":\"Handled\"}\n";
        for (const auto &event : burst) {
            expected.append(*NiriWire::encode(*NiriIpc::encode(event)));
            bytes += expected.last() + '\n';
        }
        QLocalServer replacement;
        QVERIFY(replacement.listen(stream.directory.filePath("replacement")));
        bool burstSent = false;
        connect(&replacement, &QLocalServer::newConnection, &replacement, [&] {
            auto *peer = replacement.nextPendingConnection();
            connect(peer, &QLocalSocket::readyRead, &replacement, [&, peer] {
                if (peer->canReadLine() && peer->readLine() == "\"EventStream\"\n") {
                    peer->write(bytes);
                    peer->flush();
                    burstSent = true;
                }
            });
        });
        QList<QByteArray> received;
        bool inProjection = false;
        const auto publication = connect(session, &NiriSession::eventFrame, &replacement,
                                         [&](const auto &document, quint64 epoch) {
                                             QVERIFY(!inProjection);
                                             QCOMPARE(epoch, session->state().epoch());
                                             received.append(document.bytes);
                                             QCOMPARE(state->initialStateReceived(),
                                                      received.size() == burst.size());
                                         });
        bool entered = false;
        const auto observer =
                connect(session, summary ? &NiriSession::stateUpdated : &NiriSession::stateProject,
                        &replacement, [&](const NiriEventStreamState::Changes &changes) {
                            if (entered || (cause == "event" ? changes.reset : !changes.reset))
                                return;
                            if (cause != "event" && changes.reason != cause)
                                return;
                            entered = true;
                            inProjection = true;
                            session->connectToSocket(replacement.fullServerName());
                            QEventLoop nested;
                            QTimer poll;
                            poll.setInterval(1);
                            connect(&poll, &QTimer::timeout, &nested, [&] {
                                if (burstSent) {
                                    poll.stop();
                                    QTimer::singleShot(30, &nested, &QEventLoop::quit);
                                }
                            });
                            QTimer::singleShot(2000, &nested, &QEventLoop::quit);
                            poll.start();
                            nested.exec();
                            QVERIFY(burstSent);
                            QVERIFY(received.isEmpty());
                            inProjection = false;
                        });
        if (cause == "Connecting") {
            session->stop();
            session->start();
        } else if (cause == "EndpointChanged") {
            session->selectEndpoint(stream.directory.filePath("unused"));
        } else if (cause == "Stopped") {
            session->stop();
        } else if (cause == "ProtocolError") {
            stream.send("{\"WindowClosed\":{\"id\":999}}");
        } else {
            stream.send("{\"OverviewOpenedOrClosed\":{\"is_open\":false}}");
        }
        QTRY_VERIFY(entered);
        QTRY_COMPARE(received, expected);
        disconnect(observer);
        disconnect(publication);
        QCOMPARE(session->state().epoch(), session->epoch());
        QVERIFY(state->initialStateReceived());
        QCOMPARE(state->windowCount(), 1);
        QCOMPARE(state->workspaceCount(), 1);
        QCOMPARE(windows.rowCount(), 1);
        QCOMPARE(workspaces.rowCount(), 1);
        record.isUrgent = true;
        QCOMPARE(window.window(), std::optional<NiriWindow>(record));
        QCOMPARE(workspace.workspace(), std::optional<NiriWorkspace>(wr));
        QCOMPARE(state->nativeWindow(17), window.window());
        QCOMPARE(state->nativeWorkspace(18), workspace.workspace());
        QCOMPARE(
                windows.data(windows.index(0, 0), NiriWindowsModel::WindowRole).value<NiriWindow>(),
                record);
        QCOMPARE(workspaces.data(workspaces.index(0, 0), NiriWorkspacesModel::WorkspaceRole)
                         .value<NiriWorkspace>(),
                 wr);
        QVERIFY(state->overviewOpen() && state->configFailed());
        QCOMPARE(state->keyboardLayouts()->names, QStringList{"de"});
        QCOMPARE(state->castCount(), 0);
    }
    void reset_data()
    {
        QTest::addColumn<QString>("cause");
        QTest::newRow("disconnect") << QString("disconnect");
        QTest::newRow("endpoint") << QString("endpoint");
        QTest::newRow("protocol") << QString("protocol");
        QTest::newRow("reentrant-reset") << QString("reentrant-reset");
    }
    void reset()
    {
        QFETCH(QString, cause);
        StateStreamFixture stream;
        QVERIFY(stream.start());
        auto *session = NiriSession::instance();
        QQmlEngine engine;
        auto *state = NiriState::create(&engine, &engine);
        const auto defaults = observable(state);
        NiriWindowsModel windows;
        NiriWorkspacesModel workspaces;
        NiriEventLog log;
        log.setCapacity(2);
        QAbstractItemModelTester lt(&log, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester wt(&windows,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester wst(&workspaces,
                                     QAbstractItemModelTester::FailureReportingMode::QtTest);
        NiriWindowReactive window;
        window.setWindowId(7);
        NiriWorkspaceReactive workspace;
        workspace.setWorkspaceId(8);
        const auto invalidWindow = observable(&window);
        const auto invalidWorkspace = observable(&workspace);
        QVERIFY(populate(stream));
        QCOMPARE(state->windowCount(), 1);
        QCOMPARE(state->workspaceCount(), 1);
        QVERIFY(state->initialStateReceived());
        QVERIFY(window.valid() && workspace.valid());
        const auto populated = observable(state);
        QCOMPARE(log.rowCount(), 2);
        const auto retainedLogTail = log.data(log.index(1, 0), NiriEventLog::NameRole);
        QMap<QString, std::shared_ptr<QSignalSpy>> spies;
        const auto *meta = state->metaObject();
        for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
            const auto property = meta->property(i);
            QVERIFY(property.hasNotifySignal());
            spies.insert(property.name(),
                         std::make_shared<QSignalSpy>(state, property.notifySignal()));
        }
        QSignalSpy windowsRemoved(&windows, &QAbstractItemModel::rowsRemoved);
        QSignalSpy workspacesRemoved(&workspaces, &QAbstractItemModel::rowsRemoved);
        QSignalSpy windowChanged(&window, &NiriWindowReactive::windowChanged);
        QSignalSpy workspaceChanged(&workspace, &NiriWorkspaceReactive::workspaceChanged);
        bool repeatedReset = false;
        connect(&windows, &QAbstractItemModel::rowsAboutToBeRemoved, this, [&] {
            if (cause == "reentrant-reset" && !repeatedReset) {
                repeatedReset = true;
                session->stop();
            }
        });
        if (cause == "disconnect") {
            stream.peer->disconnectFromServer();
        } else if (cause == "endpoint") {
            session->selectEndpoint(stream.directory.filePath("other-socket"));
        } else if (cause == "reentrant-reset") {
            session->stop();
        } else {
            stream.send("{\"WindowClosed\":{\"id\":999}}");
        }
        QTRY_VERIFY(!state->initialStateReceived());
        QCOMPARE(observable(state), defaults);
        QCOMPARE(observable(&window), invalidWindow);
        QCOMPARE(observable(&workspace), invalidWorkspace);
        QCOMPARE(windows.rowCount(), 0);
        QCOMPARE(workspaces.rowCount(), 0);
        QCOMPARE(windowsRemoved.size(), 1);
        QCOMPARE(workspacesRemoved.size(), 1);
        QCOMPARE(windowChanged.size(), 1);
        QCOMPARE(workspaceChanged.size(), 1);
        QCOMPARE(log.rowCount(), 2);
        QCOMPARE(log.data(log.index(1, 0), NiriEventLog::NameRole), retainedLogTail);
        for (auto it = populated.cbegin(); it != populated.cend(); ++it) {
            if (it.value() != defaults.value(it.key()))
                QVERIFY2(!spies[it.key()]->isEmpty(),
                         qPrintable("Missing reset NOTIFY for " + it.key()));
        }
        session->connectToSocket(stream.server.fullServerName());
        QTRY_VERIFY(session->subscribed());
        QVERIFY(populate(stream));
        QCOMPARE(observable(state), populated);
        QCOMPARE(windows.rowCount(), 1);
        QCOMPARE(workspaces.rowCount(), 1);
        QVERIFY(window.valid() && workspace.valid());
        QVERIFY(state->initialStateReceived());
    }
};

QTEST_MAIN(TestProjectionReset)
#include "test_projection_reset.moc"
