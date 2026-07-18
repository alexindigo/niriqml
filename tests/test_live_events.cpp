// Live e2e tests for typed event dispatch (NiriEvents).
//
// Uses NiriEvents cached snapshot getters to verify gadget decoding shape
// without depending on NiriRequests (introduced later).

#include "test_live_common.h"

#include <QProcess>

#include "nirievents.h"
#include "niritypes.h"

class TestLiveEvents : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        setupNiriConnection();
        // Warm up: wait for initial snapshot to be cached
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::windowsChanged);
        if (spy.count() == 0)
            spy.wait(3000);
    }
    void cleanupTestCase() { NiriConnection::instance()->disconnect(); }

    void rawEventCatchall();
    void gadgetShape_window();
    void gadgetShape_workspace();
    void keyboardLayoutsPopulated();
};

void TestLiveEvents::rawEventCatchall()
{
    QSignalSpy spy(NiriEvents::instance(), &NiriEvents::rawEvent);
    // Reliably fire OverviewOpenedOrClosed by toggling overview twice
    QProcess::execute("niri", { "msg", "action", "toggle-overview" });
    QProcess::execute("niri", { "msg", "action", "toggle-overview" });
    QVERIFY(spy.wait(5000));
}

void TestLiveEvents::gadgetShape_window()
{
    QVariantList windows = NiriEvents::instance()->lastWindowsSnapshot();
    QVERIFY(!windows.isEmpty());
    NiriWindow w = windows.first().value<NiriWindow>();
    QVERIFY(w.id != 0);
    // Layout fields decoded correctly (tile_size is doubles, window_size is ints)
    QCOMPARE(w.layout.tileSize.size(), 2);
    QCOMPARE(w.layout.windowSize.size(), 2);
}

void TestLiveEvents::gadgetShape_workspace()
{
    QVariantList wss = NiriEvents::instance()->lastWorkspacesSnapshot();
    QVERIFY(!wss.isEmpty());
    NiriWorkspace ws = wss.first().value<NiriWorkspace>();
    QVERIFY(ws.id != 0);
    QVERIFY(!ws.output.isEmpty());
}

void TestLiveEvents::keyboardLayoutsPopulated()
{
    NiriKeyboardLayouts kl = NiriEvents::instance()->lastKeyboardLayouts();
    QVERIFY(!kl.names.isEmpty());
}

QTEST_MAIN(TestLiveEvents)
#include "test_live_events.moc"
