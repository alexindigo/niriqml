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
    QVERIFY(!w.title.has_value());
    QVERIFY(!w.appId.has_value());
}

QTEST_MAIN(TestTypes)
#include "test_types.moc"
