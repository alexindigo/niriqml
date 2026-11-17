#include "nirievents.h"
#include "nirieventlog.h"
#include "niristate.h"
#include "niriwindowreactive.h"
#include "niriworkspacereactive.h"
#include "niriwindowsmodel.h"
#include "niriworkspacesmodel.h"
#include "generated/niriipccodec_generated.h"
#include "ipcsurfacefixtures.h"
#include "streamfixture.h"
#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <QRegularExpression>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QSignalSpy>
#include <limits>

namespace {
using S = NiriEventStreamState;
constexpr quint64 Max = std::numeric_limits<quint64>::max();
NiriWindow win(quint64 id, bool focused = false)
{
    NiriWindow w;
    w.id = id;
    w.title = QString::number(id);
    w.appId = "app";
    w.pid = -42;
    w.workspaceId = Max;
    w.isFocused = focused;
    w.layout.tilePosInWorkspaceView = std::array<double, 2>{11, 12};
    w.focusTimestamp = NiriTimestamp{};
    return w;
}
NiriWorkspace ws(quint64 id, QString name = {}, std::optional<QString> output = {},
                 bool focused = false)
{
    NiriWorkspace w;
    w.id = id;
    w.idx = 3;
    w.name = name;
    w.output = output;
    w.isActive = w.isFocused = focused;
    w.activeWindowId = Max;
    return w;
}
bool send(StateStreamFixture &stream, const NiriEvent &event)
{
    const auto old = NiriSession::instance()->state().revision();
    stream.send(*NiriWire::encode(*NiriIpc::encode(event)));
    return QTest::qWaitFor([old] { return NiriSession::instance()->state().revision() > old; });
}
bool seed(StateStreamFixture &stream)
{
    NiriCast cast;
    cast.streamId = Max;
    cast.sessionId = Max;
    cast.kind = NiriCastKind(NiriCastKind::PipeWire);
    cast.target = NiriCastTarget::fromNothing({});
    return send(stream,
                NiriEvent::fromWorkspacesChanged({{ws(Max, "named", QString("A"), true)}, {}}))
            && send(stream, NiriEvent::fromWindowsChanged({{win(Max, true)}, {}}))
            && send(stream, NiriEvent::fromKeyboardLayoutsChanged({{0, {"us", "de"}, {}}, {}}))
            && send(stream, NiriEvent::fromOverviewOpenedOrClosed({true, {}}))
            && send(stream, NiriEvent::fromConfigLoaded({true, {}}))
            && send(stream, NiriEvent::fromCastsChanged({{cast}, {}}));
}
QList<QVariant> properties(QObject *object)
{
    QList<QVariant> values;
    const auto *meta = object->metaObject();
    for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i)
        values.append(meta->property(i).read(object));
    return values;
}
void compareModels(QAbstractItemModel &early, QAbstractItemModel &late)
{
    QCOMPARE(early.rowCount(), late.rowCount());
    QCOMPARE(early.roleNames(), late.roleNames());
    for (int row = 0; row < early.rowCount(); ++row) {
        for (int role : early.roleNames().keys())
            QCOMPARE(early.data(early.index(row, 0), role), late.data(late.index(row, 0), role));
    }
}
QSet<int> roles(const QSignalSpy &spy, int row)
{
    const auto list = spy.at(row).at(2).value<QList<int>>();
    return QSet<int>(list.cbegin(), list.cend());
}
} // namespace

class TestStateProjections : public QObject
{
    Q_OBJECT
private slots:
    void init() { NiriSession::instance()->stop(); }
    void cleanup() { NiriSession::instance()->stop(); }

    void lateConsumers_data()
    {
        QTest::addColumn<QByteArray>("eventBytes");
        const auto fixtures =
                NiriSurfaceTest::loadFixture(QFINDTESTDATA("fixtures/serde-v26.04/vectors.json"));
        QSet<QString> seen;
        for (const auto &row : *fixtures.lookup("surfaceVectors")->arrayValue()) {
            if (*row.lookup("type")->stringValue() != "Event")
                continue;
            const auto &value = *row.lookup("value");
            const auto tag = value.objectValue()->first().first;
            if (!seen.contains(tag)) {
                seen.insert(tag);
                QTest::newRow(qPrintable(tag)) << *NiriWire::encode(value);
            }
        }
        QCOMPARE(seen.size(), 19);
    }
    void lateConsumers()
    {
        QFETCH(QByteArray, eventBytes);
        StateStreamFixture stream;
        QVERIFY(stream.start());
        QQmlEngine earlyEngine;
        auto *early = NiriState::create(&earlyEngine, &earlyEngine);
        NiriWindowsModel windows;
        NiriWorkspacesModel workspaces;
        QAbstractItemModelTester wt(&windows,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester wst(&workspaces,
                                     QAbstractItemModelTester::FailureReportingMode::QtTest);
        NiriWindowReactive window;
        window.setWindowId(Max);
        NiriWorkspaceReactive workspace;
        workspace.setWorkspaceId(Max);
        QVERIFY(seed(stream));
        const auto old = NiriSession::instance()->state().revision();
        stream.send(eventBytes);
        QTRY_VERIFY(NiriSession::instance()->state().revision() > old);
        QQmlEngine lateEngine;
        auto *late = NiriState::create(&lateEngine, &lateEngine);
        NiriWindowsModel lateWindows;
        NiriWorkspacesModel lateWorkspaces;
        NiriWindowReactive lateWindow;
        lateWindow.setWindowId(Max);
        NiriWorkspaceReactive lateWorkspace;
        lateWorkspace.setWorkspaceId(Max);
        QCOMPARE(properties(early), properties(late));
        QCOMPARE(early->snapshot(), late->snapshot());
        QCOMPARE(early->windowsSnapshot(), late->windowsSnapshot());
        QCOMPARE(early->workspacesSnapshot(), late->workspacesSnapshot());
        QCOMPARE(early->castsSnapshot(), late->castsSnapshot());
        QCOMPARE(properties(&window), properties(&lateWindow));
        QCOMPARE(properties(&workspace), properties(&lateWorkspace));
        compareModels(windows, lateWindows);
        compareModels(workspaces, lateWorkspaces);
        auto *events = NiriEvents::instance();
        QCOMPARE(early->windowsSnapshot(), events->lastWindowsSnapshot());
        QCOMPARE(early->workspacesSnapshot(), events->lastWorkspacesSnapshot());
        QCOMPARE(early->castsSnapshot(), events->lastCastsSnapshot());
        QCOMPARE(early->keyboardLayouts(), events->lastKeyboardLayouts());
        QCOMPARE(early->overviewOpen(), events->lastOverviewOpen());
        QCOMPARE(early->configFailed(), events->lastConfigFailed());
        QCOMPARE(early->focusedWindowId(), events->lastFocusedWindowId());
        for (const auto &record : early->nativeWindowsSnapshot()) {
            NiriWindowReactive a, b;
            a.setWindowId(record.id);
            b.setWindowId(record.id);
            QCOMPARE(a.window(), std::optional<NiriWindow>(record));
            QCOMPARE(properties(&a), properties(&b));
            QCOMPARE(early->nativeWindow(record.id), std::optional<NiriWindow>(record));
        }
        for (const auto &record : early->nativeWorkspacesSnapshot()) {
            NiriWorkspaceReactive a;
            a.setWorkspaceId(record.id);
            QCOMPARE(a.workspace(), std::optional<NiriWorkspace>(record));
            QCOMPARE(early->nativeWorkspace(record.id), std::optional<NiriWorkspace>(record));
        }
        for (const auto &record : early->nativeCastsSnapshot())
            QCOMPARE(early->nativeCast(record.streamId), std::optional<NiriCast>(record));
        QCOMPARE(stream.queries, 0);
    }

    void fieldCoverage()
    {
        StateStreamFixture stream;
        QVERIFY(stream.start());
        NiriWindowsModel windows;
        NiriWorkspacesModel workspaces;
        NiriWindowReactive w;
        NiriWorkspaceReactive workspace;
        w.setWindowId(0);
        workspace.setWorkspaceId(0);
        auto record = win(0, true);
        record.isUrgent = record.isFloating = true;
        auto wr = ws(0, "name", QString("output"), true);
        wr.isUrgent = true;
        QVERIFY(send(stream, NiriEvent::fromWindowsChanged({{record}, {}})));
        QVERIFY(send(stream, NiriEvent::fromWorkspacesChanged({{wr}, {}})));
        QSignalSpy wc(&windows, &QAbstractItemModel::dataChanged);
        QSignalSpy wsc(&workspaces, &QAbstractItemModel::dataChanged);
        QSignalSpy title(&w, &NiriWindowReactive::titleChanged);
        QSignalSpy focus(&w, &NiriWindowReactive::isFocusedChanged);
        QSignalSpy count(NiriState::instance(), &NiriState::windowCountChanged);
        const QList<std::pair<std::function<void(NiriWindow &)>, int>> changes{
                {[](auto &v) { v.title.reset(); }, NiriWindowsModel::TitleRole},
                {[](auto &v) { v.appId.reset(); }, NiriWindowsModel::AppIdRole},
                {[](auto &v) { v.pid.reset(); }, NiriWindowsModel::PidRole},
                {[](auto &v) { v.workspaceId.reset(); }, NiriWindowsModel::WorkspaceIdRole},
                {[](auto &v) { v.focusTimestamp.reset(); }, NiriWindowsModel::FocusTimestampRole},
                {[](auto &v) { v.layout = {}; }, NiriWindowsModel::LayoutRole},
                {[](auto &v) { v.isFocused = false; }, NiriWindowsModel::IsFocusedRole},
                {[](auto &v) { v.isFloating = false; }, NiriWindowsModel::IsFloatingRole},
                {[](auto &v) { v.isUrgent = false; }, NiriWindowsModel::IsUrgentRole},
                {[](auto &v) { v.extensions = {{"future", NiriWire::WireValue(true)}}; },
                 NiriWindowsModel::WindowRole}};
        for (const auto &change : changes) {
            wc.clear();
            change.first(record);
            QVERIFY(send(stream, NiriEvent::fromWindowOpenedOrChanged({record, {}})));
            QCOMPARE(wc.size(), 1);
            QCOMPARE(roles(wc, 0), (QSet<int>{NiriWindowsModel::WindowRole, change.second}));
            QCOMPARE(w.window(), std::optional<NiriWindow>(record));
            QCOMPARE(windows.data(windows.index(0, 0), NiriWindowsModel::WindowRole)
                             .value<NiriWindow>(),
                     record);
        }
        QCOMPARE(title.size(), 1);
        QCOMPARE(focus.size(), 1);
        QCOMPARE(count.size(), 0);
        const QList<std::pair<std::function<void(NiriWorkspace &)>, int>> workspaceChanges{
                {[](auto &v) { v.name.reset(); }, NiriWorkspacesModel::NameRole},
                {[](auto &v) { v.output.reset(); }, NiriWorkspacesModel::OutputRole},
                {[](auto &v) { v.activeWindowId.reset(); },
                 NiriWorkspacesModel::ActiveWindowIdRole},
                {[](auto &v) { v.idx = 4; }, NiriWorkspacesModel::IdxRole},
                {[](auto &v) { v.isFocused = false; }, NiriWorkspacesModel::IsFocusedRole},
                {[](auto &v) { v.isActive = false; }, NiriWorkspacesModel::IsActiveRole},
                {[](auto &v) { v.isUrgent = false; }, NiriWorkspacesModel::IsUrgentRole},
                {[](auto &v) { v.extensions = {{"future", NiriWire::WireValue(true)}}; },
                 NiriWorkspacesModel::WorkspaceRole}};
        for (const auto &change : workspaceChanges) {
            wsc.clear();
            change.first(wr);
            QVERIFY(send(stream, NiriEvent::fromWorkspacesChanged({{wr}, {}})));
            QCOMPARE(wsc.size(), 1);
            QCOMPARE(roles(wsc, 0), (QSet<int>{NiriWorkspacesModel::WorkspaceRole, change.second}));
            QCOMPARE(workspace.workspace(), std::optional<NiriWorkspace>(wr));
            QCOMPARE(workspaces.data(workspaces.index(0, 0), NiriWorkspacesModel::WorkspaceRole)
                             .value<NiriWorkspace>(),
                     wr);
        }
    }

    void modelProtocolAndExactNotifications()
    {
        StateStreamFixture stream;
        QVERIFY(stream.start());
        NiriWindowsModel windows;
        NiriWorkspacesModel workspaces;
        QAbstractItemModelTester wt(&windows,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester wst(&workspaces,
                                     QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(send(stream, NiriEvent::fromWindowsChanged({{win(3), win(1, true)}, {}})));
        QVERIFY(send(stream, NiriEvent::fromWorkspacesChanged({{ws(3), ws(1)}, {}})));
        QPersistentModelIndex win3(windows.index(1, 0));
        QPersistentModelIndex ws3(workspaces.index(1, 0));
        QSignalSpy reset(&windows, &QAbstractItemModel::modelReset);
        QSignalSpy wsreset(&workspaces, &QAbstractItemModel::modelReset);
        QSignalSpy wc(&windows, &QAbstractItemModel::dataChanged);
        QSignalSpy wsc(&workspaces, &QAbstractItemModel::dataChanged);
        auto *state = NiriState::instance();
        QSignalSpy stateChanged(state, &NiriState::stateChanged);
        QSignalSpy title(state, &NiriState::focusedWindowTitleChanged);
        QSignalSpy count(state, &NiriState::windowCountChanged);
        NiriWindowReactive selected;
        selected.setWindowId(1);
        QSignalSpy rt(&selected, &NiriWindowReactive::titleChanged);
        QSignalSpy rl(&selected, &NiriWindowReactive::layoutChanged);
        QSignalSpy rr(&selected, &NiriWindowReactive::windowChanged);
        NiriWindowLayout layout;
        layout.tileSize = {100, 200};
        QVERIFY(send(
                stream,
                NiriEvent::fromWindowLayoutsChanged({{std::make_tuple(quint64(1), layout)}, {}})));
        QCOMPARE(wc.size(), 1);
        QCOMPARE(roles(wc, 0),
                 (QSet<int>{NiriWindowsModel::WindowRole, NiriWindowsModel::LayoutRole}));
        QCOMPARE(title.size(), 0);
        QCOMPARE(count.size(), 0);
        QCOMPARE(rt.size(), 0);
        QCOMPARE(rl.size(), 1);
        QCOMPARE(rr.size(), 1);
        wc.clear();
        QVERIFY(send(stream, NiriEvent::fromWindowFocusChanged({3, {}})));
        QCOMPARE(wc.size(), 2);
        for (int i = 0; i < wc.size(); ++i)
            QCOMPARE(roles(wc, i),
                     (QSet<int>{NiriWindowsModel::WindowRole, NiriWindowsModel::IsFocusedRole}));
        wc.clear();
        const auto stateCount = stateChanged.size();
        const auto reactiveCount = rr.size();
        QVERIFY(send(stream, NiriEvent::fromWindowsChanged({state->nativeWindowsSnapshot(), {}})));
        QVERIFY(send(stream,
                     NiriEvent::fromWorkspacesChanged({state->nativeWorkspacesSnapshot(), {}})));
        QCOMPARE(wc.size(), 0);
        QCOMPARE(wsc.size(), 0);
        QCOMPARE(stateChanged.size(), stateCount);
        QCOMPARE(rr.size(), reactiveCount);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(wsreset.size(), 0);
        bool oldReadable = false, oldWorkspaceReadable = false;
        connect(&windows, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                [&](const auto &, int first, int) {
                    oldReadable = windows.data(windows.index(first, 0), NiriWindowsModel::TitleRole)
                                          .toString()
                            == "1";
                });
        connect(&workspaces, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                [&](const auto &, int first, int) {
                    oldWorkspaceReadable =
                            workspaces.data(workspaces.index(first, 0), NiriWorkspacesModel::IdRole)
                                    .toString()
                            == "1";
                });
        QVERIFY(send(stream, NiriEvent::fromWindowsChanged({{win(0), win(2), win(3, true)}, {}})));
        QVERIFY(send(stream, NiriEvent::fromWorkspacesChanged({{ws(0), ws(2), ws(3)}, {}})));
        QVERIFY(oldReadable && oldWorkspaceReadable);
        QVERIFY(win3.isValid() && ws3.isValid());
        QCOMPARE(win3.row(), 2);
        QCOMPARE(ws3.row(), 2);
        QCOMPARE(windows.data(win3, NiriWindowsModel::IdRole).toString(), "3");
        QCOMPARE(workspaces.data(ws3, NiriWorkspacesModel::IdRole).toString(), "3");
        NiriSession::instance()->stop();
        QCOMPARE(windows.rowCount(), 0);
        QCOMPARE(workspaces.rowCount(), 0);
        QVERIFY(!win3.isValid() && !ws3.isValid());
        QCOMPARE(reset.size(), 0);
        QCOMPARE(wsreset.size(), 0);
        NiriSession::instance()->start();
        QTRY_VERIFY(NiriSession::instance()->subscribed());
        QVERIFY(send(stream, NiriEvent::fromWindowsChanged({{win(Max)}, {}})));
        QVERIFY(send(stream, NiriEvent::fromWorkspacesChanged({{ws(Max)}, {}})));
        QCOMPARE(windows.rowCount(), 1);
        QCOMPARE(workspaces.rowCount(), 1);
    }

    void focusMatrix()
    {
        StateStreamFixture stream;
        QVERIFY(stream.start());
        NiriWindowsModel windows;
        NiriWorkspacesModel workspaces;
        NiriWindowReactive one, two;
        one.setWindowId(1);
        two.setWindowId(2);
        NiriWorkspaceReactive a, b;
        a.setWorkspaceId(1);
        b.setWorkspaceId(2);
        auto *state = NiriState::instance();
        QVERIFY(send(stream, NiriEvent::fromWindowsChanged({{win(1, true), win(2)}, {}})));
        QVERIFY(send(stream,
                     NiriEvent::fromWorkspacesChanged(
                             {{ws(1, "a", QString("A"), true), ws(2, "b", QString("B"))}, {}})));
        QVERIFY(send(stream, NiriEvent::fromWindowOpenedOrChanged({win(2, true), {}})));
        QVERIFY(!one.isFocused() && two.isFocused());
        QCOMPARE(state->focusedWindowId(), std::optional<quint64>(2));
        QVERIFY(!windows.data(windows.index(0, 0), NiriWindowsModel::IsFocusedRole).toBool());
        QVERIFY(send(stream, NiriEvent::fromWindowOpenedOrChanged({win(2), {}})));
        QVERIFY(!state->focusedWindowId());
        QVERIFY(!one.isFocused() && !two.isFocused());
        QVERIFY(send(stream, NiriEvent::fromWorkspaceActivated({true, 2, {}})));
        QVERIFY(!a.isFocused() && b.isFocused());
        QVERIFY(a.isActive() && b.isActive());
        QCOMPARE(state->activeOutput(), std::optional<QString>("B"));
        QVERIFY(send(stream, NiriEvent::fromWorkspaceActiveWindowChanged({Max, 2, {}})));
        QCOMPARE(b.activeWindowId(), std::optional<quint64>(Max));
        for (auto id :
             {std::optional<quint64>(1), std::optional<quint64>(), std::optional<quint64>(999)}) {
            QVERIFY(send(stream, NiriEvent::fromWindowFocusChanged({id, {}})));
            QCOMPARE(state->focusedWindowId(), id == 1 ? id : std::nullopt);
            QCOMPARE(one.isFocused(), id == 1);
            QVERIFY(!two.isFocused());
        }
        QVERIFY(send(stream, NiriEvent::fromWindowsChanged({{win(1)}, {}})));
        QVERIFY(!state->focusedWindowId() && !state->focusedWindowTitle());
        QVERIFY(send(stream, NiriEvent::fromWindowsChanged({{}, {}})));
        QVERIFY(send(stream, NiriEvent::fromWorkspacesChanged({{}, {}})));
        QVERIFY(!state->focusedWindowId() && !state->focusedWorkspaceId()
                && !state->activeOutput());
        QVERIFY(!one.valid() && !two.valid() && !a.valid() && !b.valid());
        QCOMPARE(windows.rowCount(), 0);
        QCOMPARE(workspaces.rowCount(), 0);
    }

    void selectors()
    {
        StateStreamFixture stream;
        QVERIFY(stream.start());
        QVERIFY(send(stream, NiriEvent::fromWindowsChanged({{win(0), win(1), win(2)}, {}})));
        QVERIFY(send(stream,
                     NiriEvent::fromWorkspacesChanged(
                             {{ws(0, "zero"), ws(1, "A"), ws(2, "B"), ws(3, "A")}, {}})));
        NiriWorkspaceReactive w;
        QVERIFY(!w.valid());
        w.setWorkspaceId(2);
        w.setWorkspaceName("A");
        QCOMPARE(w.id(), quint64(1));
        QSignalSpy record(&w, &NiriWorkspaceReactive::workspaceChanged);
        w.setWorkspaceId(0);
        QCOMPARE(w.id(), quint64(1));
        QCOMPARE(record.size(), 0);
        w.setWorkspaceId(std::nullopt);
        QCOMPARE(w.id(), quint64(1));
        w.setWorkspaceId(2);
        QVERIFY(send(stream,
                     NiriEvent::fromWorkspacesChanged(
                             {NiriSession::instance()->state().workspacesSnapshot(), {}})));
        QCOMPARE(w.id(), quint64(1));
        w.setWorkspaceName({});
        QCOMPARE(w.id(), quint64(2));
        w.setWorkspaceName("missing");
        QVERIFY(!w.valid());
        w.setWorkspaceName({});
        QCOMPARE(w.id(), quint64(2));
        w.setWorkspaceId(0);
        QVERIFY(w.valid());
        QCOMPARE(w.id(), quint64(0));
        w.setWorkspaceId(std::nullopt);
        QVERIFY(!w.valid());
        QCOMPARE(w.qmlId(), QVariant::fromValue(nullptr));
        w.setWorkspaceId(2);
        bool coherent = false;
        connect(&w, &NiriWorkspaceReactive::workspaceNameChanged, this, [&] {
            if (w.workspaceName() == "A") {
                coherent = w.valid() && w.id() == 1 && w.name() == "A";
                w.setWorkspaceName("B");
                w.setWorkspaceId(0);
            }
        });
        w.setWorkspaceName("A");
        QVERIFY(coherent);
        QCOMPARE(w.workspaceName(), "B");
        QCOMPARE(w.workspaceId(), std::optional<quint64>(0));
        QCOMPARE(w.id(), quint64(2));
        QCOMPARE(w.name(), std::optional<QString>("B"));
        w.setWorkspaceName({});
        QCOMPARE(w.id(), quint64(0));
        NiriWindowReactive window;
        window.setWindowId(0);
        QVERIFY(window.valid());
        bool windowCoherent = false;
        connect(&window, &NiriWindowReactive::windowIdChanged, this, [&] {
            if (window.windowId() == 1) {
                windowCoherent = window.valid() && window.id() == 1 && window.title() == "1";
                window.setWindowId(2);
            }
        });
        window.setWindowId(1);
        QVERIFY(windowCoherent);
        QCOMPARE(window.windowId(), std::optional<quint64>(2));
        QCOMPARE(window.id(), quint64(2));
        QCOMPARE(window.title(), std::optional<QString>("2"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*unsigned integer out of range.*"));
        window.setQmlWindowId(QStringLiteral("18446744073709551616"));
        QCOMPARE(window.windowId(), std::optional<quint64>(2));
        QPointer<NiriWindowReactive> disposable = new NiriWindowReactive;
        connect(disposable, &NiriWindowReactive::windowIdChanged, this, [&] { delete disposable; });
        disposable->setWindowId(1);
        QVERIFY(!disposable);
        QPointer<NiriWorkspaceReactive> disposableWs = new NiriWorkspaceReactive;
        connect(disposableWs, &NiriWorkspaceReactive::workspaceNameChanged, this,
                [&] { delete disposableWs; });
        disposableWs->setWorkspaceName("A");
        QVERIFY(!disposableWs);
        w.setWorkspaceName("B");
        QVERIFY(send(stream,
                     NiriEvent::fromWorkspacesChanged({{ws(0, "B"), ws(2, "renamed")}, {}})));
        QCOMPARE(w.id(), quint64(0));
        QVERIFY(send(stream, NiriEvent::fromWorkspacesChanged({{ws(2, "renamed")}, {}})));
        QVERIFY(!w.valid());
    }

    void publicationReentrancy()
    {
        StateStreamFixture stream;
        QVERIFY(stream.start());
        NiriWindowsModel windows;
        NiriWorkspacesModel workspaces;
        QAbstractItemModelTester wt(&windows,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester wst(&workspaces,
                                     QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(seed(stream));
        std::unique_ptr<NiriWindowsModel> late;
        std::unique_ptr<NiriWorkspacesModel> lateWs;
        connect(&windows, &QAbstractItemModel::rowsAboutToBeRemoved, this, [&] {
            if (!late) {
                late = std::make_unique<NiriWindowsModel>();
                lateWs = std::make_unique<NiriWorkspacesModel>();
            }
        });
        QVERIFY(send(stream, NiriEvent::fromWindowsChanged({{win(1), win(2)}, {}})));
        compareModels(windows, *late);
        compareModels(workspaces, *lateWs);
        bool stopped = false;
        connect(&windows, &QAbstractItemModel::rowsAboutToBeInserted, this, [&] {
            if (!stopped) {
                stopped = true;
                NiriSession::instance()->stop();
            }
        });
        const auto old = NiriSession::instance()->epoch();
        stream.send(*NiriWire::encode(
                *NiriIpc::encode(NiriEvent::fromWindowOpenedOrChanged({win(3), {}}))));
        QTRY_VERIFY(NiriSession::instance()->epoch() != old);
        QCOMPARE(windows.rowCount(), 0);
        QCOMPARE(workspaces.rowCount(), 0);
        QCOMPARE(late->rowCount(), 0);
        QVERIFY(!NiriState::instance()->initialStateReceived());
    }

    void qmlLateLoader()
    {
        StateStreamFixture stream;
        QVERIFY(stream.start());
        QVERIFY(seed(stream));
        const QList<quint64> ids{0, 9007199254740993ULL, 9223372036854775808ULL, Max};
        QList<NiriWindow> windows;
        QList<NiriWorkspace> workspaces;
        for (auto id : ids) {
            windows.append(win(id, id == Max));
            workspaces.append(ws(id, QString::number(id), QString("output"), id == Max));
        }
        QVERIFY(send(stream, NiriEvent::fromWindowsChanged({windows, {}})));
        QVERIFY(send(stream, NiriEvent::fromWorkspacesChanged({workspaces, {}})));
        QQmlEngine engine;
        QQmlComponent component(&engine,
                                QUrl::fromLocalFile(QFINDTESTDATA("state-projections.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("loadConsumers", true);
        QTRY_VERIFY(root->property("passed").toBool());
        QCOMPARE(stream.queries, 0);
    }
};

QTEST_MAIN(TestStateProjections)
#include "test_state_projections.moc"
