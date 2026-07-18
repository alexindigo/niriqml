// Live e2e tests for write path: NiriActions, NiriRequests typed queries,
// and sendRaw escape hatch.

#include "test_live_common.h"

#include <QJsonObject>

#include "niriactions.h"
#include "nirievents.h"
#include "niripendingreply.h"
#include "nirirequests.h"
#include "niritypes.h"

class TestLiveWrite : public QObject
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

    // Actions
    void actionSpawn_reachable();
    void actionFocusWindow_okForValidId();
    void actionCloseWindow_okForBogusId();

    // Typed queries
    void queryWindowsReturnsList();
    void queryWorkspacesReturnsList();
    void queryOutputsReturnsMap();
    void queryFocusedWindowReturnsGadget();
    void queryKeyboardLayoutsReturnsGadget();
    void queryVersionReturnsString();

    // Raw escape hatch
    void sendRawStringQuery();
    void sendRawObjectRequest();
};

void TestLiveWrite::actionSpawn_reachable()
{
    NiriPendingReply *reply = NiriActions::instance()->spawn({"true"});
    QSignalSpy spy(reply, &NiriPendingReply::finished);
    QVERIFY(spy.wait(3000));
    QVERIFY(!reply->isError());
}

void TestLiveWrite::actionFocusWindow_okForValidId()
{
    NiriPendingReply *r = NiriRequests::instance()->focusedWindow();
    QSignalSpy rSpy(r, &NiriPendingReply::finished);
    QVERIFY(rSpy.wait(3000));
    if (r->value().isNull())
        QSKIP("No focused window available");
    NiriWindow fw = r->value().value<NiriWindow>();

    NiriPendingReply *reply = NiriActions::instance()->focusWindow(fw.id);
    QSignalSpy spy(reply, &NiriPendingReply::finished);
    QVERIFY(spy.wait(3000));
    QVERIFY(!reply->isError());
}

void TestLiveWrite::actionCloseWindow_okForBogusId()
{
    NiriPendingReply *reply = NiriActions::instance()->closeWindow(9999999999ULL);
    QSignalSpy spy(reply, &NiriPendingReply::finished);
    QVERIFY(spy.wait(3000));
    QVERIFY(!reply->isError());
}

void TestLiveWrite::queryWindowsReturnsList()
{
    NiriPendingReply *reply = NiriRequests::instance()->windows();
    QSignalSpy spy(reply, &NiriPendingReply::finished);
    QVERIFY(spy.wait(3000));
    QVERIFY(!reply->isError());
    QVERIFY(!reply->value().toList().isEmpty());
}

void TestLiveWrite::queryWorkspacesReturnsList()
{
    NiriPendingReply *reply = NiriRequests::instance()->workspaces();
    QSignalSpy spy(reply, &NiriPendingReply::finished);
    QVERIFY(spy.wait(3000));
    QVERIFY(!reply->isError());
    QVERIFY(!reply->value().toList().isEmpty());
}

void TestLiveWrite::queryOutputsReturnsMap()
{
    NiriPendingReply *reply = NiriRequests::instance()->outputs();
    QSignalSpy spy(reply, &NiriPendingReply::finished);
    QVERIFY(spy.wait(3000));
    QVERIFY(!reply->isError());
    QVariantMap outputs = reply->value().toMap();
    QVERIFY(!outputs.isEmpty());
    NiriOutput first = outputs.first().value<NiriOutput>();
    QVERIFY(!first.name.isEmpty());
    QVERIFY(!first.modes.isEmpty());
}

void TestLiveWrite::queryFocusedWindowReturnsGadget()
{
    NiriPendingReply *reply = NiriRequests::instance()->focusedWindow();
    QSignalSpy spy(reply, &NiriPendingReply::finished);
    QVERIFY(spy.wait(3000));
    QVERIFY(!reply->isError());
    if (!reply->value().isNull()) {
        NiriWindow w = reply->value().value<NiriWindow>();
        QVERIFY(w.id != 0);
    }
}

void TestLiveWrite::queryKeyboardLayoutsReturnsGadget()
{
    NiriPendingReply *reply = NiriRequests::instance()->keyboardLayouts();
    QSignalSpy spy(reply, &NiriPendingReply::finished);
    QVERIFY(spy.wait(3000));
    QVERIFY(!reply->isError());
    NiriKeyboardLayouts kl = reply->value().value<NiriKeyboardLayouts>();
    QVERIFY(!kl.names.isEmpty());
}

void TestLiveWrite::queryVersionReturnsString()
{
    NiriPendingReply *reply = NiriRequests::instance()->version();
    QSignalSpy spy(reply, &NiriPendingReply::finished);
    QVERIFY(spy.wait(3000));
    QVERIFY(!reply->isError());
    QVERIFY(!reply->value().toString().isEmpty());
}

void TestLiveWrite::sendRawStringQuery()
{
    NiriPendingReply *reply = NiriRequests::instance()->sendRaw(QStringLiteral("Version"));
    QSignalSpy spy(reply, &NiriPendingReply::finished);
    QVERIFY(spy.wait(3000));
    QVERIFY(!reply->isError());
    QVariantMap result = reply->value().toMap();
    QVERIFY(result.contains("Version"));
    QVERIFY(!result.value("Version").toString().isEmpty());
}

void TestLiveWrite::sendRawObjectRequest()
{
    NiriPendingReply *fw = NiriRequests::instance()->focusedWindow();
    QSignalSpy fwSpy(fw, &NiriPendingReply::finished);
    QVERIFY(fwSpy.wait(3000));
    if (fw->value().isNull())
        QSKIP("No focused window available");
    quint64 id = fw->value().value<NiriWindow>().id;

    QJsonObject request;
    request["Action"] = QJsonObject{{"FocusWindow", QJsonObject{{"id", qint64(id)}}}};
    NiriPendingReply *reply = NiriRequests::instance()->sendRaw(request);
    QSignalSpy spy(reply, &NiriPendingReply::finished);
    QVERIFY(spy.wait(3000));
    QVERIFY(!reply->isError());
}

QTEST_MAIN(TestLiveWrite)
#include "test_live_write.moc"
