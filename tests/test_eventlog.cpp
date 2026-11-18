#include "nirieventlog.h"
#include "nirisession.h"
#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTest>

class TestEventLog : public QObject
{
    Q_OBJECT
private slots:
    void init() { NiriSession::instance()->stop(); }
    void cleanup() { NiriSession::instance()->stop(); }
    void capacities_data()
    {
        QTest::addColumn<int>("capacity");
        for (int c : {-1, 0, 1, 500})
            QTest::newRow(qPrintable(QString::number(c))) << c;
    }
    void capacities()
    {
        QFETCH(int, capacity);
        NiriEventLog log;
        QAbstractItemModelTester tester(&log,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy removal(&log, &QAbstractItemModel::rowsRemoved);
        QSignalSpy insertion(&log, &QAbstractItemModel::rowsInserted);
        QSignalSpy changed(&log, &NiriEventLog::capacityChanged);
        log.setCapacity(capacity);
        QCOMPARE(log.capacity(), qMax(0, capacity));
        const auto notifications = changed.size();
        log.setCapacity(qMax(0, capacity));
        QCOMPARE(changed.size(), notifications);
        const int n = 2 * qMax(1, capacity) + 3;
        for (int i = 0; i < n; ++i) {
            log.appendEvent(QString::number(i), i);
            QCOMPARE(log.rowCount(), qMin(i + 1, qMax(0, capacity)));
            if (log.rowCount()) {
                QCOMPARE(log.data(log.index(log.rowCount() - 1, 0), NiriEventLog::NameRole)
                                 .toString(),
                         QString::number(i));
                QCOMPARE(log.data(log.index(0, 0), NiriEventLog::PayloadRole).toInt(),
                         i - log.rowCount() + 1);
            }
        }
        QCOMPARE(insertion.size(), capacity > 0 ? n : 0);
        QCOMPARE(removal.size(), capacity > 0 ? n - capacity : 0);
        for (const auto &row : removal) {
            QCOMPARE(row[1].toInt(), 0);
            QCOMPARE(row[2].toInt(), 0);
        }
        log.setCapacity(0);
        QCOMPARE(log.rowCount(), 0);
        const auto removals = removal.size();
        log.setCapacity(-1);
        log.appendEvent("disabled", {});
        QCOMPARE(removal.size(), removals);
        QCOMPARE(log.rowCount(), 0);
        log.setCapacity(1);
        log.appendEvent("enabled", {});
        QCOMPARE(log.rowCount(), 1);
    }
    void shrinkAndPersistentIndexes()
    {
        NiriEventLog log;
        QAbstractItemModelTester tester(&log,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        log.setCapacity(5);
        for (int i = 0; i < 5; ++i)
            log.appendEvent(QString::number(i), {});
        QPersistentModelIndex retained(log.index(4, 0));
        QPersistentModelIndex evicted(log.index(0, 0));
        QSignalSpy removal(&log, &QAbstractItemModel::rowsRemoved);
        log.setCapacity(2);
        QCOMPARE(removal.size(), 1);
        QCOMPARE(removal[0][1].toInt(), 0);
        QCOMPARE(removal[0][2].toInt(), 2);
        QVERIFY(!evicted.isValid());
        QVERIFY(retained.isValid());
        QCOMPARE(retained.row(), 1);
        QCOMPARE(log.data(retained, NiriEventLog::NameRole).toString(), "4");
        log.appendEvent("5", {});
        QCOMPARE(retained.row(), 0);
        log.setCapacity(10);
        QCOMPARE(removal.size(), 2);
        log.setCapacity(0);
        QVERIFY(!retained.isValid());
    }
    void reentrantWrites()
    {
        NiriEventLog log;
        QAbstractItemModelTester tester(&log,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        bool requested = false;
        int depth = 0;
        connect(&log, &QAbstractItemModel::rowsAboutToBeInserted, this, [&] {
            QCOMPARE(++depth, 1);
            if (!requested) {
                requested = true;
                log.setCapacity(0);
                log.appendEvent("nested-disabled", {});
            }
        });
        connect(&log, &QAbstractItemModel::rowsInserted, this, [&] { QCOMPARE(depth--, 1); });
        connect(&log, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                [&] { QCOMPARE(++depth, 1); });
        connect(&log, &QAbstractItemModel::rowsRemoved, this, [&] { QCOMPARE(depth--, 1); });
        connect(&log, &NiriEventLog::capacityChanged, this,
                [&] { QVERIFY(log.rowCount() <= log.capacity()); });
        log.appendEvent("outer", {});
        QCOMPARE(depth, 0);
        QCOMPARE(log.capacity(), 0);
        QCOMPARE(log.rowCount(), 0);
        log.setCapacity(1);
        log.appendEvent("first", {});
        bool shrinkRequested = false;
        connect(&log, &QAbstractItemModel::rowsAboutToBeRemoved, this, [&] {
            if (!shrinkRequested) {
                shrinkRequested = true;
                log.setCapacity(-1);
                log.appendEvent("nested", {});
            }
        });
        log.appendEvent("wrap", {});
        QCOMPARE(log.capacity(), 0);
        QCOMPARE(log.rowCount(), 0);
        QCOMPARE(depth, 0);
    }
};

QTEST_MAIN(TestEventLog)
#include "test_eventlog.moc"
