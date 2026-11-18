#include "nirieventstreamstate.h"
#include "nirievents.h"
#include "niristate.h"
#include "nirirequests.h"
#include "generated/niriipccodec_generated.h"
#include "ipcfixtures.h"
#include "ipcsurfacefixtures.h"
#include "streamfixture.h"
#include <QJsonDocument>
#include <QQmlComponent>
#include <QSignalSpy>
#include <QThread>
#include <limits>

namespace {
using State = NiriEventStreamState;
NiriEvent stateEvent(const QByteArray &bytes)
{
    const auto document = NiriWire::parse(bytes);
    NiriEvent value;
    NiriIpc::DecodeError error;
    if (!document || !NiriIpc::decodeEvent(document->root, value, error))
        qFatal("Bad state test event: %s", qPrintable(error.path + error.message));
    return value;
}
NiriWindow window(quint64 id, bool focused = false)
{
    NiriWindow w;
    w.id = id;
    w.isFocused = focused;
    return w;
}
NiriWorkspace workspace(quint64 id, std::optional<QString> output, bool focused = false)
{
    NiriWorkspace w;
    w.id = id;
    w.output = output;
    w.isActive = w.isFocused = focused;
    return w;
}
void seed(State &s)
{
    s.apply(NiriEvent::fromWindowsChanged({{window(1, true), window(2)}, {}}), s.epoch());
    s.apply(NiriEvent::fromWorkspacesChanged(
                    {{workspace(1, QString("A"), true), workspace(2, QString("B"))}, {}}),
            s.epoch());
}
QByteArray encoded(const NiriEvent &e)
{
    return *NiriWire::encode(*NiriIpc::encode(e));
}
} // namespace

class TestState : public QObject
{
    Q_OBJECT
private slots:
    void init() { NiriSession::instance()->stop(); }
    void cleanup() { NiriSession::instance()->stop(); }
    void optionalTouchpadState()
    {
        State s;
        QVERIFY(!s.touchpadOn());
        QVERIFY(!s.initialStateReceived());
        const auto baseline = s.replicate();
        QCOMPARE(baseline.last().kind(), NiriEvent::CastsChanged);
        const auto off = NiriEvent::fromTouchpadToggled({false, {}});
        auto result = s.apply(off, 0);
        QVERIFY(result.changes.touchpad && result.changes.changed());
        QCOMPARE(s.touchpadOn(), std::optional<bool>(false));
        QVERIFY(!s.initialStateReceived());
        QVERIFY(!s.apply(off, 0).changes.changed());
        const auto replica = s.replicate();
        QCOMPARE(replica[replica.size() - 2].kind(), NiriEvent::CastsChanged);
        QCOMPARE(replica.last().kind(), NiriEvent::TouchpadToggled);
        State restored;
        for (const auto &event : replica)
            restored.apply(event, 0);
        QCOMPARE(restored.touchpadOn(), s.touchpadOn());
        QVERIFY(restored.initialStateReceived());
        QVERIFY(s.reset(1, "replacement").touchpad);
        QVERIFY(!s.touchpadOn());
        QCOMPARE(s.apply(off, 0).status, State::ApplyResult::StaleEpoch);
        QVERIFY(!s.touchpadOn());
    }
    void touchpadConsumerReadinessAndReset()
    {
        StateStreamFixture stream;
        QVERIFY(stream.start());
        auto *state = NiriState::instance();
        auto *events = NiriEvents::instance();
        QSignalSpy changed(state, &NiriState::touchpadOnChanged);
        QSignalSpy ready(state, &NiriState::initialStateReceivedChanged);
        QQmlEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import Niri
            QtObject {
                property var on: NiriState.touchpadOn
                property bool ready: NiriState.initialStateReceived
            }
        )",
                          QUrl());
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QCOMPARE(root->property("on").metaType(), QMetaType::fromType<std::nullptr_t>());
        State empty;
        for (const auto &event : empty.replicate())
            stream.send(encoded(event));
        QTRY_VERIFY(root->property("ready").toBool());
        QCOMPARE(ready.size(), 1);
        QCOMPARE(changed.size(), 0);
        QVERIFY(!state->touchpadOn());
        QCOMPARE(root->property("on").metaType(), QMetaType::fromType<std::nullptr_t>());
        bool committedBeforeEvent = false;
        QObject observerContext;
        const auto observer =
                connect(events, &NiriEvents::touchpadToggled, &observerContext,
                        [&](bool on) { committedBeforeEvent = state->touchpadOn() == on; });
        stream.send("{\"TouchpadToggled\":{\"on\":false}}");
        QTRY_COMPARE(changed.size(), 1);
        QVERIFY(committedBeforeEvent);
        QCOMPARE(root->property("on").metaType(), QMetaType::fromType<bool>());
        QCOMPARE(root->property("on").toBool(), false);
        QCOMPARE(state->snapshot()["touchpadOn"].toBool(), false);
        QScopedPointer<NiriState> late(NiriState::create(&engine, &engine));
        QCOMPARE(late->touchpadOn(), std::optional<bool>(false));
        stream.send("{\"TouchpadToggled\":{\"on\":true}}");
        QTRY_COMPARE(changed.size(), 2);
        QCOMPARE(root->property("on").toBool(), true);
        QCOMPARE(late->touchpadOn(), std::optional<bool>(true));
        stream.send("{\"TouchpadToggled\":{\"on\":true}}");
        QTest::qWait(20);
        QCOMPARE(changed.size(), 2);
        QCOMPARE(ready.size(), 1);
        NiriSession::instance()->stop();
        QCOMPARE(changed.size(), 3);
        QVERIFY(!late->touchpadOn());
        QCOMPARE(root->property("on").metaType(), QMetaType::fromType<std::nullptr_t>());
        QVERIFY(!root->property("ready").toBool());
        disconnect(observer);
    }
    void newerCastsRemainDistinct()
    {
        State s;
        NiriCast a, b;
        a.streamId = 1;
        b.streamId = 2;
        a.sessionId = b.sessionId = 99;
        a.kind = NiriCastKind(NiriCastKind::ExtImageCopyCapture);
        b.kind = NiriCastKind(NiriCastKind::ExtImageCopyCaptureCursor);
        a.target = b.target = NiriCastTarget::fromNothing({});
        s.apply(NiriEvent::fromCastsChanged({{a, b}, {}}), 0);
        QCOMPARE(s.castCount(), 2);
        QCOMPARE(s.cast(1)->kind.kind(), NiriCastKind::ExtImageCopyCapture);
        QCOMPARE(s.cast(2)->kind.kind(), NiriCastKind::ExtImageCopyCaptureCursor);
        StateStreamFixture stream;
        QVERIFY(stream.start());
        stream.send(encoded(NiriEvent::fromCastsChanged({{a, b}, {}})));
        auto *state = NiriState::instance();
        QTRY_COMPARE(state->castCount(), 2);
        QCOMPARE(state->nativeCast(1)->kind.kind(), NiriCastKind::ExtImageCopyCapture);
        QCOMPARE(state->nativeCast(2)->kind.kind(), NiriCastKind::ExtImageCopyCaptureCursor);
        QQmlEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import Niri
            QtObject {
                property bool observed: NiriState.castsByStreamId["1"].kind.known
                    && NiriState.castsByStreamId["1"].kind.kind === NiriCastKind.ExtImageCopyCapture
                    && NiriState.castsByStreamId["2"].kind.kind === NiriCastKind.ExtImageCopyCaptureCursor
                    && NiriState.castsByStreamId["1"].sessionId === NiriState.castsByStreamId["2"].sessionId
            }
        )",
                          QUrl());
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QVERIFY(root->property("observed").toBool());
        s.apply(NiriEvent::fromCastStopped({1, {}}), 0);
        QVERIFY(!s.cast(1));
        QVERIFY(s.cast(2));
    }
    void focusMatrix()
    {
        State s;
        seed(s);
        auto incoming = window(2, true);
        auto r = s.apply(NiriEvent::fromWindowOpenedOrChanged({incoming, {}}), 0);
        QCOMPARE(s.focusedWindowId(), std::optional<quint64>(2));
        QVERIFY(!s.window(1)->isFocused);
        QCOMPARE(r.changes.windows[1].fields, quint64(State::WindowFocused));
        incoming.isFocused = false;
        s.apply(NiriEvent::fromWindowOpenedOrChanged({incoming, {}}), 0);
        QVERIFY(!s.focusedWindowId());
        for (auto bytes :
             {"{\"WindowFocusChanged\":{\"id\":1}}", "{\"WindowFocusChanged\":{\"id\":null}}",
              "{\"WindowFocusChanged\":{\"id\":999}}"}) {
            s.apply(stateEvent(bytes), 0);
            QCOMPARE(s.focusedWindowId().has_value(), QByteArray(bytes).contains(":1}"));
        }
        // No phantom focus when the unknown record eventually opens.
        s.apply(NiriEvent::fromWindowOpenedOrChanged({window(999), {}}), 0);
        QVERIFY(!s.focusedWindowId());
        s.apply(NiriEvent::fromWindowsChanged({{window(1)}, {}}), 0);
        QVERIFY(!s.focusedWindowId());
        s.apply(NiriEvent::fromWindowsChanged({{}, {}}), 0);
        QVERIFY(!s.focusedWindowId());
        QVERIFY(s.windowsSnapshot().isEmpty());
    }
    void activationAndDanglingReferences()
    {
        State s;
        s.apply(NiriEvent::fromWorkspacesChanged(
                        {{workspace(1, QString("A"), true), workspace(2, QString("B"), true),
                          workspace(3, QString("B")), workspace(4, std::nullopt, true),
                          workspace(5, std::nullopt)},
                         {}}),
                0);
        s.apply(stateEvent("{\"WorkspaceActivated\":{\"id\":3,\"focused\":true}}"), 0);
        QVERIFY(s.workspace(1)->isActive);
        QVERIFY(!s.workspace(1)->isFocused);
        QVERIFY(!s.workspace(2)->isActive);
        QVERIFY(s.workspace(3)->isFocused);
        s.apply(stateEvent("{\"WorkspaceActivated\":{\"id\":5,\"focused\":false}}"), 0);
        QVERIFY(!s.workspace(4)->isActive);
        QVERIFY(s.workspace(5)->isActive);
        QCOMPARE(s.focusedWorkspaceId(), std::optional<quint64>(3));
        s.apply(stateEvent("{\"WorkspaceActiveWindowChanged\":{\"workspace_id\":5,\"active_window_"
                           "id\":999}}"),
                0);
        QCOMPARE(s.workspace(5)->activeWindowId, std::optional<quint64>(999));
        s.apply(stateEvent("{\"WorkspaceActiveWindowChanged\":{\"workspace_id\":5,\"active_window_"
                           "id\":null}}"),
                0);
        QVERIFY(!s.workspace(5)->activeWindowId);
        auto w = window(1);
        w.workspaceId = 999;
        QCOMPARE(s.apply(NiriEvent::fromWindowOpenedOrChanged({w, {}}), 0).status,
                 State::ApplyResult::Applied);
    }
    void replacementsAndBeforeImages()
    {
        State s;
        seed(s);
        auto w = *s.window(1);
        w.title = "title";
        w.appId = "app";
        w.pid = 0;
        w.workspaceId = 42;
        w.focusTimestamp = NiriTimestamp{};
        w.layout.tilePosInWorkspaceView = std::array<double, 2>{1, 2};
        w.layout.posInScrollingLayout = std::array<quint64, 2>{3, 4};
        s.apply(NiriEvent::fromWindowOpenedOrChanged({w, {}}), 0);
        const auto before = s.window(1);
        NiriWindowLayout layout;
        layout.tileSize = {100, 200};
        const auto change = std::make_tuple(quint64(1), layout);
        auto r = s.apply(NiriEvent::fromWindowLayoutsChanged({{change, change}, {}}), 0);
        QCOMPARE(r.changes.windows.size(), 1);
        QCOMPARE(r.changes.windows[1].fields, quint64(State::Layout));
        QCOMPARE(r.changes.windows[1].before, before);
        QVERIFY(before->layout.tilePosInWorkspaceView);
        QVERIFY(!s.window(1)->layout.tilePosInWorkspaceView);
        QVERIFY(!s.window(1)->layout.posInScrollingLayout);
        QCOMPARE(s.window(1)->layout, layout);
        s.apply(NiriEvent::fromWindowOpenedOrChanged({window(1), {}}), 0);
        QVERIFY(!s.window(1)->title && !s.window(1)->pid && !s.window(1)->focusTimestamp);
        QVERIFY(!s.window(1)->appId && !s.window(1)->workspaceId);
        const auto equal = s.window(1);
        r = s.apply(NiriEvent::fromWindowOpenedOrChanged({*equal, {}}), 0);
        QVERIFY(!r.changes.changed());
        QCOMPARE(s.window(1), equal);
        r = s.apply(stateEvent("{\"WindowClosed\":{\"id\":1}}"), 0);
        QVERIFY(r.changes.windows[1].before && !r.changes.windows[1].after);
        QCOMPARE(r.changes.windows[1].before, equal);
        auto ws = workspace(1, QString("A"), true);
        ws.name = "named";
        ws.activeWindowId = 999;
        s.apply(NiriEvent::fromWorkspacesChanged({{ws}, {}}), 0);
        s.apply(NiriEvent::fromWorkspacesChanged({{workspace(1, {})}, {}}), 0);
        QVERIFY(!s.workspace(1)->name && !s.workspace(1)->output
                && !s.workspace(1)->activeWindowId);
    }
    void allEventsAndReplicate()
    {
        const auto fixtures =
                NiriSurfaceTest::loadFixture(QFINDTESTDATA("fixtures/serde-v26.04/vectors.json"));
        QSet<int> kinds;
        for (const auto &row : *fixtures.lookup("surfaceVectors")->arrayValue()) {
            if (*row.lookup("type")->stringValue() != "Event")
                continue;
            NiriEvent e;
            NiriIpc::DecodeError detail;
            QVERIFY(NiriIpc::decodeEvent(*row.lookup("value"), e, detail));
            kinds.insert(e.kind());
            State s;
            // Every strict target in independent serde vectors is u64 max.
            const quint64 max = std::numeric_limits<quint64>::max();
            s.apply(NiriEvent::fromWindowsChanged({{window(max)}, {}}), 0);
            s.apply(NiriEvent::fromWorkspacesChanged({{workspace(max, {})}, {}}), 0);
            s.apply(stateEvent("{\"KeyboardLayoutsChanged\":{\"keyboard_layouts\":{\"names\":[],"
                               "\"current_idx\":0}}}"),
                    0);
            NiriCast cast;
            cast.streamId = max;
            s.apply(NiriEvent::fromCastsChanged({{cast}, {}}), 0);
            QCOMPARE(s.apply(e, 0).status, State::ApplyResult::Applied);
            State copy;
            for (const auto &part : s.replicate())
                QCOMPARE(copy.apply(part, 0).status, State::ApplyResult::Applied);
            QCOMPARE(copy.windowsSnapshot(), s.windowsSnapshot());
            QCOMPARE(copy.workspacesSnapshot(), s.workspacesSnapshot());
            QCOMPARE(copy.castsSnapshot(), s.castsSnapshot());
            QCOMPARE(copy.keyboardLayouts(), s.keyboardLayouts());
            QCOMPARE(copy.overviewOpen(), s.overviewOpen());
            QCOMPARE(copy.configFailed(), s.configFailed());
        }
        QCOMPARE(kinds.size(), 19);
    }
    void castsConfigAndNoops()
    {
        State s;
        NiriCast a, b;
        a.streamId = 1;
        b.streamId = 2;
        a.sessionId = b.sessionId = 10;
        a.pid = 1;
        a.pwNodeId = 2;
        a.target = NiriCastTarget::fromOutput({"DP-1", {}});
        s.apply(NiriEvent::fromCastsChanged({{b, a}, {}}), 0);
        QCOMPARE(s.castsSnapshot().first().streamId, quint64(1));
        a.pid.reset();
        a.pwNodeId.reset();
        a.isActive = true;
        a.target = NiriCastTarget::fromNothing({});
        const auto changed = s.apply(NiriEvent::fromCastStartedOrChanged({a, {}}), 0);
        QVERIFY(!s.cast(1)->pid && !s.cast(1)->pwNodeId);
        QCOMPARE(s.cast(1)->target, a.target);
        QCOMPARE(changed.changes.casts[1].fields,
                 quint64(State::CastPid | State::NodeId | State::CastActive | State::Target));
        s.apply(stateEvent("{\"CastStopped\":{\"stream_id\":1}}"), 0);
        QVERIFY(!s.cast(1) && s.cast(2));
        for (bool failed : {true, false, true}) {
            s.apply(NiriEvent::fromConfigLoaded({failed, {}}), 0);
            QCOMPARE(s.configFailed(), failed);
        }
        for (auto bytes :
             {"{\"WindowUrgencyChanged\":{\"id\":999,\"urgent\":true}}",
              "{\"WorkspaceUrgencyChanged\":{\"id\":999,\"urgent\":true}}",
              "{\"WindowFocusTimestampChanged\":{\"id\":999,\"focus_timestamp\":null}}",
              "{\"ScreenshotCaptured\":{\"path\":null}}", "{\"Future\":{}}"})
            QVERIFY(!s.apply(stateEvent(bytes), 0).changes.changed());
        seed(s);
        s.apply(stateEvent("{\"WindowUrgencyChanged\":{\"id\":1,\"urgent\":true}}"), 0);
        s.apply(stateEvent("{\"WorkspaceUrgencyChanged\":{\"id\":1,\"urgent\":true}}"), 0);
        s.apply(stateEvent("{\"WindowFocusTimestampChanged\":{\"id\":1,\"focus_timestamp\":{"
                           "\"secs\":0,\"nanos\":0}}}"),
                0);
        QVERIFY(s.window(1)->isUrgent && s.workspace(1)->isUrgent);
        QVERIFY(s.window(1)->focusTimestamp);
        s.apply(stateEvent("{\"WindowFocusTimestampChanged\":{\"id\":1,\"focus_timestamp\":null}}"),
                0);
        QVERIFY(!s.window(1)->focusTimestamp);
        s.apply(NiriEvent::fromCastsChanged({{}, {}}), 0);
        QVERIFY(s.castsSnapshot().isEmpty());
    }
    void keyboardAndScalars()
    {
        State s;
        QVERIFY(!s.keyboardLayouts());
        NiriKeyboardLayouts layouts;
        layouts.names = {"us"};
        s.apply(NiriEvent::fromKeyboardLayoutsChanged({layouts, {}}), 0);
        auto r = s.apply(NiriEvent::fromKeyboardLayoutSwitched({255, {}}), 0);
        QVERIFY(r.changes.keyboardLayouts);
        QCOMPARE(s.keyboardLayouts()->currentIdx, quint8(255));
        QCOMPARE(s.keyboardLayouts()->names, QList<QString>{"us"});
        QVERIFY(!s.apply(NiriEvent::fromKeyboardLayoutSwitched({255, {}}), 0).changes.changed());
        s.apply(NiriEvent::fromKeyboardLayoutsChanged({{}, {}}), 0);
        QVERIFY(s.keyboardLayouts() && s.keyboardLayouts()->names.isEmpty());
        s.apply(NiriEvent::fromOverviewOpenedOrClosed({true, {}}), 0);
        QVERIFY(s.overviewOpen());
        QVERIFY(!s.apply(NiriEvent::fromOverviewOpenedOrClosed({true, {}}), 0).changes.changed());
        s.reset(1, "disconnect");
        QVERIFY(!s.keyboardLayouts() && !s.overviewOpen());
    }
    void impossibleEvents_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        for (auto bytes :
             {"{\"WorkspaceActivated\":{\"id\":999,\"focused\":true}}",
              "{\"WorkspaceActiveWindowChanged\":{\"workspace_id\":999}}",
              "{\"WindowClosed\":{\"id\":999}}", "{\"KeyboardLayoutSwitched\":{\"idx\":1}}",
              "{\"CastStopped\":{\"stream_id\":999}}"})
            QTest::newRow(bytes) << QByteArray(bytes);
    }
    void impossibleEvents()
    {
        QFETCH(QByteArray, bytes);
        State s;
        seed(s);
        const auto old = s.window(1);
        auto r = s.apply(stateEvent(bytes), 0);
        QCOMPARE(r.status, State::ApplyResult::ProtocolError);
        QVERIFY(!r.changes.changed());
        QCOMPARE(s.window(1), old);
        StateStreamFixture fixture;
        QVERIFY(fixture.start());
        QSignalSpy errors(NiriSession::instance(), &NiriSession::errorChanged);
        QSignalSpy raw(NiriEvents::instance(), &NiriEvents::rawEvent);
        fixture.send(bytes);
        QTRY_COMPARE(NiriSession::instance()->status(), NiriSession::Failed);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(raw.size(), 0);
        QCOMPARE(NiriSession::instance()->error().value("code").toString(), "ProtocolError");
        QVERIFY(!NiriState::instance()->initialStateReceived());
    }
    void layoutAtomicRejection()
    {
        State s;
        seed(s);
        const auto old = s.window(1);
        NiriWindowLayout layout;
        layout.tileSize = {10, 20};
        const auto a = std::make_tuple(quint64(1), layout);
        const auto b = std::make_tuple(quint64(999), NiriWindowLayout{});
        const auto bad = NiriEvent::fromWindowLayoutsChanged({{a, b}, {}});
        QCOMPARE(s.apply(bad, 0).status, State::ApplyResult::ProtocolError);
        QCOMPARE(s.window(1), old);
        StateStreamFixture fixture;
        QVERIFY(fixture.start());
        auto *session = NiriSession::instance();
        fixture.send(encoded(NiriEvent::fromWindowsChanged({{window(1)}, {}})));
        QTRY_VERIFY(session->state().window(1));
        bool partial = false;
        const auto connection =
                connect(session, &NiriSession::stateUpdated, this, [&](const State::Changes &c) {
                    if (c.windows.contains(1) && c.windows[1].after
                        && c.windows[1].after->layout.tileSize[0] == 10)
                        partial = true;
                });
        fixture.send(encoded(bad));
        QTRY_COMPARE(session->status(), NiriSession::Failed);
        disconnect(connection);
        QVERIFY(!partial);
        QVERIFY(session->state().windowsSnapshot().isEmpty());
    }
    void readiness_data()
    {
        QTest::addColumn<bool>("layouts");
        QTest::addColumn<bool>("populated");
        QTest::newRow("absent") << false << true;
        QTest::newRow("present-empty") << true << true;
        QTest::newRow("all-empty-absent-layouts") << false << false;
        QTest::newRow("all-empty-present-layouts") << true << false;
    }
    void readiness()
    {
        QFETCH(bool, layouts);
        QFETCH(bool, populated);
        State source;
        if (populated)
            seed(source);
        source.apply(NiriEvent::fromOverviewOpenedOrClosed({true, {}}), 0);
        source.apply(NiriEvent::fromConfigLoaded({true, {}}), 0);
        if (layouts)
            source.apply(NiriEvent::fromKeyboardLayoutsChanged({{}, {}}), 0);
        const auto burst = source.replicate();
        QCOMPARE(burst.size(), layouts ? 6 : 5);
        StateStreamFixture fixture;
        QVERIFY(fixture.start());
        auto *state = NiriState::instance();
        QSignalSpy ready(state, &NiriState::initialStateReceivedChanged);
        auto *events = NiriEvents::instance();
        QSignalSpy raw(events, &NiriEvents::rawEvent);
        QQmlEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("state-readiness.qml")));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("expectLayouts", layouts);
        root->setProperty("expectedWindows", populated ? 2 : 0);
        root->setProperty("expectedFocus",
                          populated ? QVariant("1") : QVariant::fromValue(nullptr));
        bool atCasts = false;
        const auto connection =
                connect(events->nativeEvents(), &NiriNativeEvents::castsChanged, this, [&] {
                    atCasts = state->initialStateReceived()
                            && events->lastWindowsSnapshot().size() == (populated ? 2 : 0)
                            && events->lastConfigFailed() && events->lastOverviewOpen();
                });
        for (qsizetype i = 0; i < burst.size(); ++i) {
            fixture.send(encoded(burst[i]));
            QTRY_COMPARE(raw.size(), i + 1);
            QCOMPARE(state->initialStateReceived(), i == burst.size() - 1);
            QCOMPARE(root->property("ready").toBool(), state->initialStateReceived());
        }
        QVERIFY(atCasts);
        QCOMPARE(ready.size(), 1);
        QCOMPARE(root->property("notifications").toInt(), 1);
        QVERIFY(root->property("observedAtCasts").toBool());
        fixture.send(encoded(burst.last()));
        QTRY_COMPARE(raw.size(), burst.size() + 1);
        QCOMPARE(ready.size(), 1);
        QQmlEngine lateEngine;
        auto *late = NiriState::create(&lateEngine, &lateEngine);
        QVERIFY(late->initialStateReceived());
        NiriSession::instance()->stop();
        QCOMPARE(ready.size(), 2);
        QVERIFY(!late->initialStateReceived());
        QVERIFY(events->lastWindowsSnapshot().isEmpty());
        NiriSession::instance()->start();
        QTRY_VERIFY(NiriSession::instance()->subscribed());
        // Subscription alone and a partial burst never imply readiness.
        fixture.send(encoded(burst.first()));
        QTRY_COMPARE(raw.size(), burst.size() + 2);
        QVERIFY(!state->initialStateReceived());
        NiriSession::instance()->stop();
        QVERIFY(!state->initialStateReceived());
        NiriSession::instance()->start();
        QTRY_VERIFY(NiriSession::instance()->subscribed());
        for (const auto &part : burst)
            fixture.send(encoded(part));
        QTRY_VERIFY(state->initialStateReceived());
        QCOMPARE(ready.size(), 3);
        disconnect(connection);
    }
    void epochs()
    {
        State s;
        seed(s);
        auto changes = s.reset(42, "test");
        QVERIFY(changes.reset && !changes.windows.isEmpty());
        QCOMPARE(s.apply(stateEvent("{\"CastsChanged\":{\"casts\":[]}}"), 0).status,
                 State::ApplyResult::StaleEpoch);
        QVERIFY(!s.initialStateReceived());
        QCOMPARE(s.apply(stateEvent("{\"CastsChanged\":{\"casts\":[]}}"), 42).status,
                 State::ApplyResult::Applied);
        QVERIFY(s.initialStateReceived());
        QCOMPARE(s.replicate().size(), 5);
    }
    void queryDoesNotWriteStreamState()
    {
        StateStreamFixture fixture;
        QVERIFY(fixture.start());
        auto *session = NiriSession::instance();
        fixture.send(encoded(NiriEvent::fromWindowsChanged({{window(1, true)}, {}})));
        QTRY_VERIFY(session->state().window(1));
        auto *requests = NiriRequests::instance();
        std::unique_ptr<NiriWindowsReply> reply(requests->windows());
        QTRY_VERIFY(reply->completed());
        QVERIFY(!reply->isError());
        QCOMPARE(reply->result().size(), 0);
        QCOMPARE(fixture.queries, 1);
        QCOMPARE(session->state().windowsSnapshot().size(), 1);
        QCOMPARE(NiriEvents::instance()->lastFocusedWindowId(), std::optional<quint64>(1));
    }
    void stopInsideReadinessNotification()
    {
        StateStreamFixture fixture;
        QVERIFY(fixture.start());
        auto *session = NiriSession::instance();
        auto *state = NiriState::instance();
        QSignalSpy casts(NiriEvents::instance(), &NiriEvents::castsChanged);
        const auto connection = connect(state, &NiriState::initialStateReceivedChanged, this, [&] {
            if (state->initialStateReceived())
                session->stop();
        });
        fixture.send("{\"CastsChanged\":{\"casts\":[]}}\n{\"OverviewOpenedOrClosed\":{\"is_open\":"
                     "true}}");
        QTRY_COMPARE(session->status(), NiriSession::Stopped);
        disconnect(connection);
        QVERIFY(!state->initialStateReceived());
        QVERIFY(!session->state().overviewOpen());
        QCOMPARE(casts.size(), 0);
    }
    void wrongThreadState()
    {
        StateStreamFixture fixture;
        QVERIFY(fixture.start());
        auto *session = NiriSession::instance();
        fixture.send(encoded(NiriEvent::fromWindowsChanged({{window(1)}, {}})));
        QTRY_VERIFY(session->state().window(1));
        qsizetype count = -1;
        QTest::ignoreMessage(QtWarningMsg,
                             "niriqml: acquisition/use requires the QCoreApplication thread");
        std::unique_ptr<QThread> thread(
                QThread::create([&] { count = session->state().windowsSnapshot().size(); }));
        thread->start();
        QVERIFY(thread->wait(1000));
        QCOMPARE(count, 0);
        QCOMPARE(session->state().windowsSnapshot().size(), 1);
    }
    void lateSnapshots_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        const auto fixtures =
                NiriSurfaceTest::loadFixture(QFINDTESTDATA("fixtures/serde-v26.04/vectors.json"));
        for (const auto &row : *fixtures.lookup("surfaceVectors")->arrayValue()) {
            if (*row.lookup("type")->stringValue() == "Event")
                QTest::newRow(qPrintable(*row.lookup("name")->stringValue()))
                        << *NiriWire::encode(*row.lookup("value"));
        }
    }
    void lateSnapshots()
    {
        QFETCH(QByteArray, bytes);
        const quint64 max = std::numeric_limits<quint64>::max();
        State expected;
        NiriCast cast;
        cast.streamId = max;
        cast.kind = NiriCastKind(NiriCastKind::PipeWire);
        cast.target = NiriCastTarget::fromNothing({});
        expected.apply(NiriEvent::fromWindowsChanged({{window(max, true)}, {}}), 0);
        expected.apply(NiriEvent::fromWorkspacesChanged({{workspace(max, QString("A"), true)}, {}}),
                       0);
        expected.apply(NiriEvent::fromKeyboardLayoutsChanged({{}, {}}), 0);
        expected.apply(NiriEvent::fromCastsChanged({{cast}, {}}), 0);
        StateStreamFixture fixture;
        QVERIFY(fixture.start());
        auto *early = NiriEvents::instance();
        QSignalSpy raw(early, &NiriEvents::rawEvent);
        const auto burst = expected.replicate();
        for (const auto &part : burst)
            fixture.send(encoded(part));
        QTRY_COMPARE(raw.size(), burst.size());
        fixture.send(bytes);
        QCOMPARE(expected.apply(stateEvent(bytes), 0).status, State::ApplyResult::Applied);
        QTRY_COMPARE(raw.size(), burst.size() + 1);
        QQmlEngine engine;
        auto *late = NiriEvents::create(&engine, &engine);
        QCOMPARE(late->lastWindowsSnapshot(), early->lastWindowsSnapshot());
        QCOMPARE(late->lastWorkspacesSnapshot(), early->lastWorkspacesSnapshot());
        QCOMPARE(late->lastCastsSnapshot(), early->lastCastsSnapshot());
        QCOMPARE(late->qmlLastKeyboardLayouts(), early->qmlLastKeyboardLayouts());
        QCOMPARE(late->lastOverviewOpen(), expected.overviewOpen());
        QCOMPARE(late->lastConfigFailed(), expected.configFailed());
        QCOMPARE(late->lastWindowsSnapshot(),
                 NiriIpc::qmlValue(expected.windowsSnapshot()).toList());
        QCOMPARE(late->lastWorkspacesSnapshot(),
                 NiriIpc::qmlValue(expected.workspacesSnapshot()).toList());
        QCOMPARE(late->lastCastsSnapshot(), NiriIpc::qmlValue(expected.castsSnapshot()).toList());
        QCOMPARE(late->lastFocusedWindowId(), expected.focusedWindowId());
        QCOMPARE(late->qmlLastKeyboardLayouts(), NiriIpc::qmlValue(expected.keyboardLayouts()));
    }
};
QTEST_MAIN(TestState)
#include "test_state.moc"
