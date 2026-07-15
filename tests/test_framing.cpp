#include <QTest>

class TestFraming : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void emptyInput();
};

void TestFraming::initTestCase() {}

void TestFraming::emptyInput() { QVERIFY(true); }

QTEST_MAIN(TestFraming)
#include "test_framing.moc"
