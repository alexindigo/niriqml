#include <QTest>
#include <QByteArray>
#include <QList>

#include "nirilinebuffer.h"

class TestFraming : public QObject
{
    Q_OBJECT

private slots:
    void singleCompleteLine();
    void multipleLinesInOneChunk();
    void partialLineBufferedAcrossChunks();
    void emptyLinesSkipped();
    void noTrailingNewlineHoldsData();
    void clearResetsBuffer();
    void malformedJsonIsReturnedAsIs();
};

void TestFraming::singleCompleteLine()
{
    NiriLineBuffer buf;
    buf.feed(QByteArray("{\"a\":1}\n"));
    QList<QByteArray> lines = buf.takeCompleteLines();
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines[0], QByteArray("{\"a\":1}"));
    QCOMPARE(buf.pendingBytes(), 0);
}

void TestFraming::multipleLinesInOneChunk()
{
    NiriLineBuffer buf;
    buf.feed(QByteArray("{\"a\":1}\n{\"b\":2}\n{\"c\":3}\n"));
    QList<QByteArray> lines = buf.takeCompleteLines();
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines[0], QByteArray("{\"a\":1}"));
    QCOMPARE(lines[1], QByteArray("{\"b\":2}"));
    QCOMPARE(lines[2], QByteArray("{\"c\":3}"));
    QCOMPARE(buf.pendingBytes(), 0);
}

void TestFraming::partialLineBufferedAcrossChunks()
{
    NiriLineBuffer buf;
    buf.feed(QByteArray("{\"partial\":"));
    QCOMPARE(buf.takeCompleteLines().size(), 0);
    QVERIFY(buf.pendingBytes() > 0);

    buf.feed(QByteArray("42}\n"));
    QList<QByteArray> lines = buf.takeCompleteLines();
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines[0], QByteArray("{\"partial\":42}"));
    QCOMPARE(buf.pendingBytes(), 0);
}

void TestFraming::emptyLinesSkipped()
{
    NiriLineBuffer buf;
    buf.feed(QByteArray("\n\n{\"x\":1}\n\n"));
    QList<QByteArray> lines = buf.takeCompleteLines();
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines[0], QByteArray("{\"x\":1}"));
}

void TestFraming::noTrailingNewlineHoldsData()
{
    NiriLineBuffer buf;
    buf.feed(QByteArray("{\"complete\":1}\n{\"incomplete\":"));
    QList<QByteArray> lines = buf.takeCompleteLines();
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines[0], QByteArray("{\"complete\":1}"));
    QVERIFY(buf.pendingBytes() > 0);
}

void TestFraming::clearResetsBuffer()
{
    NiriLineBuffer buf;
    buf.feed(QByteArray("{\"partial\":"));
    QVERIFY(buf.pendingBytes() > 0);
    buf.clear();
    QCOMPARE(buf.pendingBytes(), 0);
    QCOMPARE(buf.takeCompleteLines().size(), 0);
}

void TestFraming::malformedJsonIsReturnedAsIs()
{
    NiriLineBuffer buf;
    buf.feed(QByteArray("not valid json{{{}}}\n"));
    QList<QByteArray> lines = buf.takeCompleteLines();
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines[0], QByteArray("not valid json{{{}}}"));
}

QTEST_MAIN(TestFraming)
#include "test_framing.moc"
