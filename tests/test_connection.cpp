#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "niriconnection.h"
#include "nirirequests.h"
#include "nirimock.h"

class TestConnection : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void connectAndReceiveEvents();
    void eventStreamDeliversInitialState();
    void mockSendsStreamEvent();
    void actionRequestReturnsOk();
    void actionRequestReturnsError();
    void disconnectDetected();

private:
    NiriMock *m_mock = nullptr;
};

void TestConnection::initTestCase()
{
    m_mock = new NiriMock(this);
    QVERIFY(m_mock->start());

    QJsonArray windows;
    QJsonObject w;
    w["id"] = 1;
    w["title"] = "Test Window";
    w["app_id"] = "test";
    w["pid"] = 1234;
    w["is_focused"] = true;
    w["is_floating"] = false;
    w["is_urgent"] = false;
    windows.append(w);

    QJsonArray workspaces;
    QJsonObject ws;
    ws["id"] = 1;
    ws["idx"] = 0;
    ws["name"] = "main";
    ws["output"] = "eDP-1";
    ws["is_active"] = true;
    ws["is_focused"] = true;
    ws["active_window_id"] = 1;
    workspaces.append(ws);

    m_mock->setWindows(windows);
    m_mock->setWorkspaces(workspaces);
}

void TestConnection::cleanupTestCase()
{
    NiriConnection::instance()->disconnect();
    m_mock->stop();
    delete m_mock;
    m_mock = nullptr;
}

void TestConnection::connectAndReceiveEvents()
{
    NiriConnection::instance()->disconnect();

    QSignalSpy eventSpy(NiriConnection::instance(), &NiriConnection::eventReceived);

    NiriConnection::instance()->connectToSocket(m_mock->socketPath());

    // Wait for events (confirms connection + subscription both worked)
    QVERIFY(eventSpy.wait(2000));
    QVERIFY(NiriConnection::instance()->isConnected());
}

void TestConnection::eventStreamDeliversInitialState()
{
    QSignalSpy eventSpy(NiriConnection::instance(), &NiriConnection::eventReceived);

    // Connect — the initial reply should deliver WindowsChanged + WorkspacesChanged
    NiriConnection::instance()->disconnect();
    NiriConnection::instance()->connectToSocket(m_mock->socketPath());

    QVERIFY(eventSpy.wait(1000));

    // Should have received at least 2 events (WindowsChanged, WorkspacesChanged)
    QVERIFY(eventSpy.count() >= 2);

    // Check the event names
    QStringList names;
    for (const auto &args : eventSpy)
        names << args[0].toString();

    QVERIFY(names.contains("WindowsChanged"));
    QVERIFY(names.contains("WorkspacesChanged"));
}

void TestConnection::mockSendsStreamEvent()
{
    QSignalSpy eventSpy(NiriConnection::instance(), &NiriConnection::eventReceived);

    // Connect fresh
    NiriConnection::instance()->disconnect();
    NiriConnection::instance()->connectToSocket(m_mock->socketPath());

    // Consume initial events
    QVERIFY(eventSpy.wait(1000));
    eventSpy.clear();

    // Mock sends a stream event
    QJsonObject payload;
    payload["window"] = QJsonObject{
        {"id", 2},
        {"title", "New Window"},
        {"app_id", "test2"},
    };
    m_mock->sendEvent(QJsonObject{{"WindowOpenedOrChanged", payload}});

    QVERIFY(eventSpy.wait(1000));
    QCOMPARE(eventSpy.count(), 1);

    QString name = eventSpy[0][0].toString();
    QCOMPARE(name, QStringLiteral("WindowOpenedOrChanged"));
}

void TestConnection::actionRequestReturnsOk()
{
    QJsonObject action;
    action["Action"] = QJsonObject{{"FocusWindow", QJsonObject{{"id", 1}}}};

    bool called = false;
    bool okResult = false;

    NiriRequests::instance()->send(action, [&](bool ok, const QJsonObject &) {
        called = true;
        okResult = ok;
    });

    QTest::qWait(500);
    QVERIFY(called);
    QVERIFY(okResult);
}

void TestConnection::actionRequestReturnsError()
{
    m_mock->setNextActionError(QStringLiteral("Window 999 not found"));

    QJsonObject action;
    action["Action"] = QJsonObject{{"FocusWindow", QJsonObject{{"id", 999}}}};

    bool called = false;
    bool okResult = true;
    QString errorMsg;

    NiriRequests::instance()->send(action, [&](bool ok, const QJsonObject &result) {
        called = true;
        okResult = ok;
        errorMsg = result["error"].toString();
    });

    QTest::qWait(500);
    QVERIFY(called);
    QVERIFY(!okResult);
    QCOMPARE(errorMsg, QStringLiteral("Window 999 not found"));
}

void TestConnection::disconnectDetected()
{
    QSignalSpy connectedSpy(NiriConnection::instance(), &NiriConnection::connectedChanged);

    NiriConnection::instance()->disconnect();
    QVERIFY(!NiriConnection::instance()->isConnected());
}

QTEST_MAIN(TestConnection)
#include "test_connection.moc"
