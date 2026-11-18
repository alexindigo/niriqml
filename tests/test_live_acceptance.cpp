#include "test_live_common.h"
#include "niriactions.h"
#include "nirievents.h"
#include "nirirequests.h"
#include "nirisession.h"
#include "niristate.h"
#include "niriwindowreactive.h"
#include "niriwindowsmodel.h"
#include "niriworkspacesmodel.h"
#include <QAbstractItemModelTester>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <algorithm>
#include <memory>

namespace {
bool complete(NiriPendingReply *reply)
{
    return waitForState([reply] { return reply->completed(); }, 5000);
}
bool handled(NiriPendingReply *reply)
{
    const std::unique_ptr<NiriPendingReply> owner(reply);
    if (!complete(reply) || reply->isError()) {
        qWarning() << reply->json() << reply->error().code << reply->error().message;
        return false;
    }
    return reply->responseKind() == "Handled";
}
int consumer(const QString &kind)
{
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    const auto path = QStringLiteral(NIRIQML_LIVE_SOURCE) + '/' + kind + "-only.qml";
    QQmlComponent component(&engine, QUrl::fromLocalFile(path));
    std::unique_ptr<QObject> object(component.create());
    if (!object) {
        qWarning() << component.errors();
        return 2;
    }
    const bool observed = waitForState(
            [&] {
                auto *model = qobject_cast<QAbstractItemModel *>(
                        object->property("model").value<QObject *>());
                if (model) {
                    if (kind == "windows") {
                        if (model->rowCount() != 3)
                            return false;
                        QSet<QString> apps;
                        for (int row = 0; row < 3; ++row)
                            apps.insert(
                                    model->data(model->index(row, 0), NiriWindowsModel::AppIdRole)
                                            .toString());
                        return apps
                                == QSet<QString>{"niriqml-e9-1", "niriqml-e9-2", "niriqml-e9-3"};
                    }
                    QSet<QString> names;
                    for (int row = 0; row < model->rowCount(); ++row)
                        names.insert(
                                model->data(model->index(row, 0), NiriWorkspacesModel::NameRole)
                                        .toString());
                    return names.contains("e9-main") && names.contains("e9-other");
                }
                return object->property("ready").toBool()
                        && object->property("observed").toInt() == 3
                        && object->property("title").toString() == "niriqml fixture 3";
            },
            10000);
    if (!observed)
        qWarning() << "Fresh QML consumer did not observe owned live fixtures:" << kind;
    return observed ? 0 : 3;
}
void compareModels(QAbstractItemModel &early, QAbstractItemModel &late)
{
    QCOMPARE(early.rowCount(), late.rowCount());
    QCOMPARE(early.roleNames(), late.roleNames());
    for (int row = 0; row < early.rowCount(); ++row)
        for (int role : early.roleNames().keys())
            QCOMPARE(early.data(early.index(row, 0), role), late.data(late.index(row, 0), role));
}
} // namespace

class TestLiveAcceptance : public QObject
{
    Q_OBJECT
private:
    quint64 id(int index) const
    {
        for (const auto &window : NiriState::instance()->nativeWindowsSnapshot())
            if (window.appId == QString("niriqml-e9-%1").arg(index))
                return window.id;
        qFatal("Missing owned fixture window");
    }
    std::optional<std::array<quint64, 2>> position(quint64 window) const
    {
        const auto value = NiriState::instance()->nativeWindow(window);
        return value ? value->layout.posInScrollingLayout : std::nullopt;
    }
    bool at(quint64 window, quint64 column, quint64 row) const
    {
        return position(window) == std::optional(std::array<quint64, 2>{column, row});
    }
    bool control(const QString &argument)
    {
        QProcess process;
        process.start("bash", {qEnvironmentVariable("NIRIQML_LIVE_CONTROL"), argument});
        if (!waitForState([&] { return process.state() == QProcess::NotRunning; }, 20000))
            return false;
        const auto diagnostics = process.readAllStandardError();
        if (process.exitCode())
            qWarning() << diagnostics;
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }
private slots:
    void initTestCase()
    {
        QVERIFY2(qEnvironmentVariable("NIRIQML_LIVE_FIXTURE") == "1",
                 "Use tests/live/run-live.sh: no ambient compositor is a fixture");
    }
    void freshQmlStartup_data()
    {
        QTest::addColumn<QString>("kind");
        for (auto kind : {"state", "windows", "workspaces"})
            QTest::newRow(kind) << QString(kind);
    }
    void freshQmlStartup()
    {
        QFETCH(QString, kind);
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--consumer", kind});
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 15000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        const auto diagnostic = child.readAllStandardError();
        QVERIFY2(child.exitCode() == 0, diagnostic.constData());
    }
    void subscriptionReadinessAndQueries()
    {
        auto *state = NiriState::instance();
        QVERIFY(!state->initialStateReceived());
        QSignalSpy ready(state, &NiriState::initialStateReceivedChanged);
        auto *requests = NiriRequests::instance();
        std::unique_ptr<NiriHandledReply> subscription(requests->eventStream());
        QVERIFY(complete(subscription.get()));
        QVERIFY(!subscription->isError());
        QCOMPARE(subscription->result().wireName(), QString("Handled"));
        QTRY_VERIFY_WITH_TIMEOUT(state->initialStateReceived(), 10000);
        QCOMPARE(ready.count(), 1);
        QVERIFY(NiriConnection::instance()->subscribed());
        QCOMPARE(state->windowCount(), 3);

        std::unique_ptr<NiriVersionReply> version(requests->version());
        QVERIFY(complete(version.get()));
        QVERIFY(!version->isError());
        QVERIFY(version->result().startsWith("26.04"));
        std::unique_ptr<NiriOutputsReply> outputs(requests->outputs());
        QVERIFY(complete(outputs.get()));
        QVERIFY(!outputs->isError());
        QVERIFY(outputs->result().contains("winit"));
        std::unique_ptr<NiriWorkspacesReply> workspaces(requests->workspaces());
        QVERIFY(complete(workspaces.get()));
        QVERIFY(!workspaces->isError());
        auto queriedWorkspaces = workspaces->result();
        std::sort(queriedWorkspaces.begin(), queriedWorkspaces.end(),
                  [](const auto &a, const auto &b) { return a.id < b.id; });
        QTRY_COMPARE(queriedWorkspaces, state->nativeWorkspacesSnapshot());
        std::unique_ptr<NiriWindowsReply> windows(requests->windows());
        QVERIFY(complete(windows.get()));
        QVERIFY(!windows->isError());
        auto queriedWindows = windows->result();
        std::sort(queriedWindows.begin(), queriedWindows.end(),
                  [](const auto &a, const auto &b) { return a.id < b.id; });
        QTRY_COMPARE(queriedWindows, state->nativeWindowsSnapshot());
        std::unique_ptr<NiriLayersReply> layers(requests->layers());
        QVERIFY(complete(layers.get()));
        QVERIFY(!layers->isError());
        QVERIFY(layers->result().isEmpty());
        std::unique_ptr<NiriKeyboardLayoutsReply> layouts(requests->keyboardLayouts());
        QVERIFY(complete(layouts.get()));
        QVERIFY(!layouts->isError());
        QCOMPARE(std::optional(layouts->result()), state->keyboardLayouts());
        QVERIFY(!layouts->result().names.isEmpty());
        std::unique_ptr<NiriFocusedWindowReply> focused(requests->focusedWindow());
        QVERIFY(complete(focused.get()));
        QVERIFY(!focused->isError());
        QCOMPARE(focused->result(), state->focusedWindow());
        QCOMPARE(focused->result()->id, id(3));
        std::unique_ptr<NiriFocusedOutputReply> output(requests->focusedOutput());
        QVERIFY(complete(output.get()));
        QVERIFY(!output->isError());
        QVERIFY(output->result().has_value());
        QCOMPARE(output->result()->name, QString("winit"));
        std::unique_ptr<NiriOverviewStateReply> overview(requests->overviewState());
        QVERIFY(complete(overview.get()));
        QVERIFY(!overview->isError());
        QCOMPARE(overview->result(), state->overview());
        std::unique_ptr<NiriCastsReply> casts(requests->casts());
        QVERIFY(complete(casts.get()));
        QVERIFY(!casts->isError());
        QCOMPARE(casts->result(), state->nativeCastsSnapshot());
        QVERIFY(casts->result().isEmpty());
        std::unique_ptr<NiriPendingReply> error(requests->returnError());
        QVERIFY(complete(error.get()));
        QVERIFY(error->isError());
        QCOMPARE(error->error().category, QString("Server"));
        QVERIFY(error->wireDocument().root.lookup("Err"));
        const auto action = NiriAction::fromFocusWindow({id(3), {}});
        QVERIFY(handled(requests->action(action)));
        const auto scale = NiriOutputAction::fromScale({NiriScaleToSet::fromAutomatic(), {}});
        std::unique_ptr<NiriOutputConfigChangedReply> config(
                requests->output(QString("niriqml-no-such-output"), scale));
        QVERIFY(complete(config.get()));
        QVERIFY(!config->isError());
        QCOMPARE(config->result().wireName(), QString("OutputWasMissing"));
    }
    void targetedActionsAndLateConsumers()
    {
        auto *state = NiriState::instance();
        auto *actions = NiriActions::instance();
        const auto one = id(1), two = id(2), three = id(3);
        NiriWindowsModel earlyWindows;
        NiriWorkspacesModel earlyWorkspaces;
        QAbstractItemModelTester windowTester(
                &earlyWindows, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester workspaceTester(
                &earlyWorkspaces, QAbstractItemModelTester::FailureReportingMode::QtTest);
        NiriWindowReactive early;
        early.setWindowId(one);
        QTRY_VERIFY(at(one, 1, 1) && at(two, 2, 1) && at(three, 3, 1));
        QCOMPARE(state->focusedWindowId(), std::optional(three));

        const auto oldWidth = state->nativeWindow(one)->layout.tileSize[0];
        const auto unrelatedWidth = state->nativeWindow(three)->layout.tileSize[0];
        QVERIFY(handled(actions->setWindowWidth(one, NiriSizeChange::fromSetFixed(280))));
        QTRY_VERIFY(state->nativeWindow(one)->layout.tileSize[0] != oldWidth);
        QCOMPARE(state->nativeWindow(one)->layout.tileSize[0], 280.0);
        QCOMPARE(state->nativeWindow(three)->layout.tileSize[0], unrelatedWidth);
        QCOMPARE(state->focusedWindowId(), std::optional(three));

        QVERIFY(handled(actions->focusWindow(one)));
        QVERIFY(handled(actions->moveColumnToIndex(quint64(3))));
        QTRY_VERIFY(at(one, 3, 1) && at(two, 1, 1) && at(three, 2, 1));
        QVERIFY(handled(actions->moveColumnToIndex(quint64(1))));
        QTRY_VERIFY(at(one, 1, 1) && at(two, 2, 1) && at(three, 3, 1));
        QVERIFY(handled(actions->focusWindow(three)));
        QVERIFY(handled(actions->consumeOrExpelWindowLeft(two)));
        QTRY_VERIFY(at(one, 1, 1) && at(two, 1, 2) && at(three, 2, 1));
        QCOMPARE(state->focusedWindowId(), std::optional(three));
        QVERIFY(handled(actions->focusWindow(two)));
        QVERIFY(handled(actions->moveWindowUp()));
        QTRY_VERIFY(at(two, 1, 1) && at(one, 1, 2));
        QVERIFY(handled(actions->moveWindowDown()));
        QTRY_VERIFY(at(one, 1, 1) && at(two, 1, 2));
        QVERIFY(handled(actions->focusWindow(three)));
        QVERIFY(handled(actions->consumeOrExpelWindowRight(two)));
        QTRY_VERIFY(at(one, 1, 1) && at(two, 2, 1) && at(three, 3, 1));
        QCOMPARE(state->focusedWindowId(), std::optional(three));

        const auto target = state->nativeWorkspaceByName("e9-other");
        QVERIFY(target.has_value());
        const auto source = state->nativeWindow(three)->workspaceId;
        QVERIFY(handled(actions->moveWindowToWorkspace(
                one, NiriWorkspaceReferenceArg::fromId(target->id), false)));
        QTRY_COMPARE(state->nativeWindow(one)->workspaceId, std::optional(target->id));
        QCOMPARE(state->nativeWindow(three)->workspaceId, source);
        QCOMPARE(state->focusedWindowId(), std::optional(three));
        QTRY_VERIFY(at(one, 1, 1));
        QVERIFY(handled(actions->openOverview()));
        QTRY_VERIFY(state->overviewOpen());
        QVERIFY(handled(actions->closeOverview()));
        QTRY_VERIFY(!state->overviewOpen());

        NiriWindowsModel lateWindows;
        NiriWorkspacesModel lateWorkspaces;
        compareModels(earlyWindows, lateWindows);
        compareModels(earlyWorkspaces, lateWorkspaces);
        NiriWindowReactive late;
        late.setWindowId(one);
        QCOMPARE(early.window(), late.window());
        QQmlEngine engine;
        auto *lateState = NiriState::create(&engine, &engine);
        QVERIFY(lateState->initialStateReceived());
        QCOMPARE(lateState->snapshot(), state->snapshot());
        QCOMPARE(NiriEvents::instance()->lastWindowsSnapshot(), state->windowsSnapshot());
        QCOMPARE(NiriEvents::instance()->lastWorkspacesSnapshot(), state->workspacesSnapshot());
    }
    void interactivePicksWaitAndCancel_data()
    {
        QTest::addColumn<bool>("color");
        QTest::newRow("window") << false;
        QTest::newRow("color") << true;
    }
    void interactivePicksWaitAndCancel()
    {
        QFETCH(bool, color);
        std::unique_ptr<NiriPendingReply> reply(
                color ? static_cast<NiriPendingReply *>(NiriRequests::instance()->pickColor())
                      : static_cast<NiriPendingReply *>(NiriRequests::instance()->pickWindow()));
        QSignalSpy finished(reply.get(), &NiriPendingReply::finished);
        QTest::qWait(5200); // Beyond the ordinary request deadline, no input in this fixture.
        QVERIFY2(!reply->completed(), qPrintable(reply->json()));
        QCOMPARE(finished.count(), 0);
        reply->cancel();
        QVERIFY(complete(reply.get()));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(reply->error().category, QString("Cancelled"));
    }
    void realCompositorRestart()
    {
        auto *state = NiriState::instance();
        QVERIFY(state->initialStateReceived());
        QSignalSpy ready(state, &NiriState::initialStateReceivedChanged);
        NiriWindowsModel windows;
        QVERIFY(control("--stop"));
        QTRY_VERIFY_WITH_TIMEOUT(!state->initialStateReceived(), 10000);
        QCOMPARE(state->windowCount(), 0);
        QCOMPARE(windows.rowCount(), 0);
        QVERIFY(control("--start"));
        QTRY_VERIFY_WITH_TIMEOUT(state->initialStateReceived(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(state->windowCount(), 3, 10000);
        QCOMPARE(windows.rowCount(), 3);
        QCOMPARE(ready.count(), 2);
        QVERIFY(NiriConnection::instance()->subscribed());
    }
    void validEmptyState()
    {
        auto *state = NiriState::instance();
        const auto windows = state->nativeWindowsSnapshot();
        for (const auto &window : windows)
            QVERIFY(handled(NiriActions::instance()->closeWindow(window.id)));
        QTRY_COMPARE_WITH_TIMEOUT(state->windowCount(), 0, 10000);
        QVERIFY(state->initialStateReceived());
        QVERIFY(!state->focusedWindowId());
        std::unique_ptr<NiriWindowsReply> query(NiriRequests::instance()->windows());
        QVERIFY(complete(query.get()));
        QVERIFY(!query->isError());
        QVERIFY(query->result().isEmpty());
        std::unique_ptr<NiriFocusedWindowReply> focus(NiriRequests::instance()->focusedWindow());
        QVERIFY(complete(focus.get()));
        QVERIFY(!focus->isError());
        QVERIFY(!focus->result());
        std::unique_ptr<NiriCastsReply> casts(NiriRequests::instance()->casts());
        QVERIFY(complete(casts.get()));
        QVERIFY(!casts->isError());
        QVERIFY(casts->result().isEmpty());
    }
    void cleanupTestCase() { NiriSession::instance()->stop(); }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc == 3 && QByteArray(argv[1]) == "--consumer")
        return consumer(QString::fromLocal8Bit(argv[2]));
    TestLiveAcceptance test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_live_acceptance.moc"
