// Live e2e tests for the wire protocol layer (NiriConnection).
//
// Skips cleanly if no niri socket is available.

#include "test_live_common.h"

#include "niriconnection.h"

class TestLiveWire : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { setupNiriConnection(); }
    void cleanupTestCase() { NiriConnection::instance()->disconnect(); }

    void connectionEstablishes();
    void shutdownIsClean();
};

void TestLiveWire::connectionEstablishes()
{
    QVERIFY(NiriConnection::instance()->isConnected());
    QVERIFY(!NiriConnection::instance()->socketPath().isEmpty());
}

void TestLiveWire::shutdownIsClean()
{
    NiriConnection *conn = NiriConnection::instance();
    conn->disconnect();
    QVERIFY(!conn->isConnected());

    QSignalSpy spy(conn, &NiriConnection::connectedChanged);
    conn->connectToSocket();
    QVERIFY(waitForState([&]() { return conn->isConnected(); }, 3000));
    QVERIFY(conn->isConnected());
    QVERIFY(spy.count() > 0);
}

QTEST_MAIN(TestLiveWire)
#include "test_live_wire.moc"
