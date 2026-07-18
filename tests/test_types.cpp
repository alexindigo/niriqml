#include <QTest>

#include "niritypes.h"

class TestTypes : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void niriWindowDefaults();
};

void TestTypes::initTestCase() { }

void TestTypes::niriWindowDefaults()
{
    NiriWindow w;
    QCOMPARE(w.id, 0u);
    QVERIFY(w.title.isEmpty());
    QVERIFY(w.appId.isEmpty());
}

QTEST_MAIN(TestTypes)
#include "test_types.moc"
