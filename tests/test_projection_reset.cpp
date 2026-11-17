#include "niristate.h"
#include "nirieventlog.h"
#include "niriwindowreactive.h"
#include "niriworkspacereactive.h"
#include "niriwindowsmodel.h"
#include "niriworkspacesmodel.h"
#include "generated/niriipccodec_generated.h"
#include "streamfixture.h"
#include <QAbstractItemModelTester>
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
