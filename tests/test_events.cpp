// Headless mock tests for NiriEvents typed dispatch.
//
// Injects synthetic events via NiriConnection::eventReceived and verifies
// that NiriEvents dispatches the correct typed signals and updates its
// cached snapshots. No live niri socket needed.

#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QSignalSpy>
#include <QTest>

#include "niriconnection.h"
#include "nirievents.h"
#include "niritypes.h"

class TestEvents : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(NiriEvents::instance());
        QVERIFY(NiriConnection::instance());
    }

    void windowsChangedDispatches()
    {
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::windowsChanged);

        QJsonArray windows;
        QJsonObject w;
        w["id"] = 1;
        w["title"] = "test";
        w["app_id"] = QStringLiteral("test.app");
        w["is_focused"] = true;
        w["is_floating"] = false;
        windows.append(w);
        emitEvent("WindowsChanged", {{"windows", windows.toVariantList()}});

        QCOMPARE(spy.count(), 1);
        QVariantList result = spy.at(0).at(0).toList();
        QCOMPARE(result.size(), 1);
        NiriWindow rw = result.at(0).value<NiriWindow>();
        QCOMPARE(rw.id, quint64(1));
        QCOMPARE(rw.title, QStringLiteral("test"));
        QVERIFY(rw.isFocused);
    }

    void windowsChangedUpdatesSnapshot()
    {
        QJsonArray windows;
        QJsonObject w;
        w["id"] = 42;
        w["title"] = "snap";
        windows.append(w);
        emitEvent("WindowsChanged", {{"windows", windows.toVariantList()}});

        QVariantList snap = NiriEvents::instance()->lastWindowsSnapshot();
        QCOMPARE(snap.size(), 1);
        QCOMPARE(snap.at(0).value<NiriWindow>().id, quint64(42));
    }

    void workspaceActivatedDispatches()
    {
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::workspaceActivated);

        emitEvent("WorkspaceActivated", {{"id", 3}, {"focused", true}});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), quint64(3));
        QVERIFY(spy.at(0).at(1).toBool());
    }

    void windowFocusChangedUpdatesCache()
    {
        // Set up a window in the snapshot first
        QJsonArray windows;
        QJsonObject w;
        w["id"] = 99;
        w["is_focused"] = false;
        windows.append(w);
        emitEvent("WindowsChanged", {{"windows", windows.toVariantList()}});

        // Now focus it
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::windowFocusChanged);
        emitEvent("WindowFocusChanged", {{"id", 99}});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), quint64(99));
        QCOMPARE(NiriEvents::instance()->lastFocusedWindowId(), quint64(99));

        // Cache updated
        NiriWindow snap = NiriEvents::instance()->lastWindowsSnapshot().at(0).value<NiriWindow>();
        QVERIFY(snap.isFocused);
    }

    void keyboardLayoutsPopulated()
    {
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::keyboardLayoutsChanged);

        QJsonObject kl;
        kl["names"] = QJsonArray{QStringLiteral("us"), QStringLiteral("fr")};
        kl["current_idx"] = 0;
        emitEvent("KeyboardLayoutsChanged", {{"keyboard_layouts", kl}});

        QCOMPARE(spy.count(), 1);
        QStringList expected = {QStringLiteral("us"), QStringLiteral("fr")};
        QCOMPARE(NiriEvents::instance()->lastKeyboardLayouts().names, expected);
    }

    void overviewOpenedOrClosed()
    {
        QSignalSpy spy(NiriEvents::instance(), &NiriEvents::overviewOpenedOrClosed);

        emitEvent("OverviewOpenedOrClosed", {{"is_open", true}});

        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(0).toBool());
        QVERIFY(NiriEvents::instance()->lastOverviewOpen());
    }

    void unknownEventFiresRawOnly()
    {
        QSignalSpy rawSpy(NiriEvents::instance(), &NiriEvents::rawEvent);
        QSignalSpy windowsSpy(NiriEvents::instance(), &NiriEvents::windowsChanged);

        emitEvent("SomeUnknownFutureEvent", {{"magic", 42}});

        QCOMPARE(rawSpy.count(), 1);
        QCOMPARE(rawSpy.at(0).at(0).toString(), QStringLiteral("SomeUnknownFutureEvent"));
        // windowsChanged should NOT fire for unknown events
        QCOMPARE(windowsSpy.count(), 0);
    }

    void multipleEventsAccumulateSnapshot()
    {
        // Window appears
        QJsonArray windows;
        QJsonObject w1, w2;
        w1["id"] = 1;
        w2["id"] = 2;
        windows.append(w1);
        windows.append(w2);
        emitEvent("WindowsChanged", {{"windows", windows.toVariantList()}});

        // Window closes
        emitEvent("WindowClosed", {{"id", 1}});

        QVariantList snap = NiriEvents::instance()->lastWindowsSnapshot();
        QCOMPARE(snap.size(), 1);
        QCOMPARE(snap.at(0).value<NiriWindow>().id, quint64(2));
    }

private:
    void emitEvent(const QString &name, const QVariantMap &payload)
    {
        QMetaObject::invokeMethod(NiriConnection::instance(), "eventReceived", Qt::DirectConnection,
                                  Q_ARG(QString, name), Q_ARG(QVariantMap, payload));
    }
};

QTEST_MAIN(TestEvents)
#include "test_events.moc"
