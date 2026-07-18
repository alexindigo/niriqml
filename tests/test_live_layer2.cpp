// Live e2e tests for Layer 2 QML-first primitives (models, state, reactives).
//
// Uses NiriEvents cached snapshots for id/name lookup — this test suite must
// not depend on NiriRequests (introduced later).

#include "test_live_common.h"

#include <QProcess>

#include "nirieventlog.h"
#include "nirievents.h"
#include "niristate.h"
#include "niritypes.h"
#include "niriwindowreactive.h"
#include "niriwindowsmodel.h"
#include "niriworkspacereactive.h"
#include "niriworkspacesmodel.h"

class TestLiveLayer2 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        setupNiriConnection();
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::windowsChanged);
        if (spy.count() == 0)
            spy.wait(3000);
    }
    void cleanupTestCase() { NiriConnection::instance()->disconnect(); }

    void niriEventLogCollects();
    void niriWindowsModelPopulates();
    void niriWorkspacesModelPopulates();
    void niriStateWindowCount();
    void niriStateWorkspaceCount();
    void niriStateActiveOutput();
    void niriWindowReactiveTracksId();
    void niriWorkspaceReactiveTracksName();

private:
    bool waitFor(const std::function<bool()> &pred, int timeoutMs = 3000)
    {
        const int step = 20;
        int waited = 0;
        while (waited < timeoutMs) {
            if (pred())
                return true;
            QTest::qWait(step);
            waited += step;
        }
        return pred();
    }
};

void TestLiveLayer2::niriEventLogCollects()
{
    NiriEventLog log;
    QProcess::execute("niri", {"msg", "action", "toggle-overview"});
    QProcess::execute("niri", {"msg", "action", "toggle-overview"});
    QVERIFY(waitFor([&]() { return log.rowCount() > 0; }, 5000));
}

void TestLiveLayer2::niriWindowsModelPopulates()
{
    NiriWindowsModel model;
    QVERIFY(waitFor([&]() { return model.rowCount() > 0; }, 5000));
}

void TestLiveLayer2::niriWorkspacesModelPopulates()
{
    NiriWorkspacesModel model;
    QVERIFY(waitFor([&]() { return model.rowCount() > 0; }, 5000));
}

void TestLiveLayer2::niriStateWindowCount()
{
    QVERIFY(NiriState::instance()->windowCount() > 0);
}

void TestLiveLayer2::niriStateWorkspaceCount()
{
    QVERIFY(NiriState::instance()->workspaceCount() > 0);
}

void TestLiveLayer2::niriStateActiveOutput()
{
    QVERIFY(!NiriState::instance()->activeOutput().isEmpty());
}

void TestLiveLayer2::niriWindowReactiveTracksId()
{
    QVariantList windows = NiriEvents::instance()->lastWindowsSnapshot();
    QVERIFY(!windows.isEmpty());
    quint64 targetId = windows.first().value<NiriWindow>().id;

    NiriWindowReactive reactive;
    reactive.setWindowId(targetId);
    QVERIFY(waitFor([&]() { return reactive.valid(); }, 3000));
    QCOMPARE(reactive.id(), targetId);
}

void TestLiveLayer2::niriWorkspaceReactiveTracksName()
{
    QVariantList wss = NiriEvents::instance()->lastWorkspacesSnapshot();
    QString targetName;
    quint64 targetId = 0;
    for (const QVariant &v : wss) {
        NiriWorkspace ws = v.value<NiriWorkspace>();
        if (!ws.name.isEmpty() && targetName.isEmpty())
            targetName = ws.name;
        if (targetId == 0)
            targetId = ws.id;
    }

    NiriWorkspaceReactive reactive;
    if (!targetName.isEmpty())
        reactive.setWorkspaceName(targetName);
    else if (targetId != 0)
        reactive.setWorkspaceId(targetId);
    else
        QSKIP("No workspaces available to track");

    QVERIFY(waitFor([&]() { return reactive.valid(); }, 3000));
}

QTEST_MAIN(TestLiveLayer2)
#include "test_live_layer2.moc"
