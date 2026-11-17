#include "nirilinebuffer.h"

#include <QTest>

class TestLineBuffer : public QObject
{
    Q_OBJECT
private slots:
    void everySplit();
    void byteAtATime();
    void exactLimit();
    void oversizedCompleteFrame();
    void endlessPartialFrame();
    void eof();
    void reset();
    void legacyCompatibility();
    void manyFramesWithoutQueue();
};

void TestLineBuffer::everySplit()
{
    const QByteArray input = "\n \t\r\n {\"id\":18446744073709551615} \r\n[1,2]\n\"hello\"\n";
    const QList<QByteArray> expected = {" {\"id\":18446744073709551615} \r", "[1,2]", "\"hello\""};
    for (qsizetype split = 0; split <= input.size(); ++split) {
        NiriLineBuffer buffer;
        QList<QByteArray> actual;
        const auto receive = [&](const QByteArray &frame) { actual.append(frame); };
        QVERIFY(buffer.feed(QByteArrayView(input).first(split), receive));
        QVERIFY(buffer.feed(QByteArrayView(input).sliced(split), receive));
        QVERIFY(buffer.finish());
        QCOMPARE(actual, expected);
        QCOMPARE(buffer.scannedBytesForTesting(), quint64(input.size()));
        QCOMPARE(buffer.pendingBytes(), qsizetype(0));
    }
}

void TestLineBuffer::byteAtATime()
{
    NiriLineBuffer buffer;
    const QByteArray frame(100000, 'a');
    int received = 0;
    const auto receive = [&](const QByteArray &actual) {
        ++received;
        QCOMPARE(actual, frame);
    };
    const QByteArray input = frame + '\n';
    for (qsizetype i = 0; i < input.size(); ++i)
        QVERIFY(buffer.feed(QByteArrayView(input).sliced(i, 1), receive));
    QCOMPARE(received, 1);
    // Counted visits, not timing: the accumulated prefix is never rescanned.
    QCOMPARE(buffer.scannedBytesForTesting(), quint64(input.size()));
}

void TestLineBuffer::exactLimit()
{
    NiriLineBuffer buffer(4);
    QList<QByteArray> frames;
    const auto receive = [&](const QByteArray &frame) { frames.append(frame); };
    QVERIFY(buffer.feed("1234", receive));
    QCOMPARE(buffer.pendingBytes(), qsizetype(4));
    QVERIFY(buffer.feed("\n1234\n", receive));
    QCOMPARE(frames, QList<QByteArray>({"1234", "1234"}));
    QVERIFY(buffer.finish());
    NiriLineBuffer emptyOnly(0);
    QVERIFY(emptyOnly.feed("\n\n", receive));
    QVERIFY(!emptyOnly.feed("x", receive));
    QCOMPARE(emptyOnly.pendingBytes(), qsizetype(0));
    QCOMPARE(emptyOnly.error().code, NiriLineBuffer::ErrorCode::FrameTooLarge);
}

void TestLineBuffer::oversizedCompleteFrame()
{
    NiriLineBuffer buffer(4);
    QList<QByteArray> frames;
    const auto receive = [&](const QByteArray &frame) { frames.append(frame); };
    QVERIFY(!buffer.feed("ok\n12345\nnext\n", receive));
    QCOMPARE(frames, QList<QByteArray>({"ok"}));
    QCOMPARE(buffer.pendingBytes(), qsizetype(4));
    QCOMPARE(buffer.error().code, NiriLineBuffer::ErrorCode::FrameTooLarge);
    QCOMPARE(buffer.error().actualBytes, qsizetype(5));
    QCOMPARE(buffer.error().allowedBytes, qsizetype(4));
    QVERIFY(!buffer.finish());
    QVERIFY(!buffer.feed("ignored\n", receive));
    QCOMPARE(frames.size(), 1);
    QCOMPARE(buffer.scannedBytesForTesting(), quint64(8));
}

void TestLineBuffer::endlessPartialFrame()
{
    NiriLineBuffer buffer(1024);
    const auto reject = [](const QByteArray &) { QFAIL("unexpected frame"); };
    const QByteArray chunk(127, 'x');
    for (int i = 0; i < 8; ++i)
        QVERIFY(buffer.feed(chunk, reject));
    QVERIFY(!buffer.feed(chunk, reject));
    QCOMPARE(buffer.pendingBytes(), qsizetype(1024));
    QCOMPARE(buffer.scannedBytesForTesting(), quint64(1025));
    for (int i = 0; i < 100; ++i)
        QVERIFY(!buffer.feed(chunk, reject));
    QCOMPARE(buffer.pendingBytes(), qsizetype(1024));
    QCOMPARE(buffer.scannedBytesForTesting(), quint64(1025));
}

void TestLineBuffer::eof()
{
    for (const auto &partial : {QByteArray("{\"x\":"), QByteArray("{}"), QByteArray(" null ")}) {
        NiriLineBuffer buffer;
        buffer.feed("{}\n" + partial);
        QCOMPARE(buffer.takeCompleteLines(), QList<QByteArray>({"{}"}));
        QVERIFY(!buffer.finish());
        QCOMPARE(buffer.error().code, NiriLineBuffer::ErrorCode::TruncatedFrame);
        QCOMPARE(buffer.error().actualBytes, partial.size());
    }
    for (const auto &partial : {QByteArray(), QByteArray(" \r\t")}) {
        NiriLineBuffer buffer;
        buffer.feed("{}\n" + partial);
        QVERIFY(buffer.finish());
        QCOMPARE(buffer.takeCompleteLines(), QList<QByteArray>({"{}"}));
        QCOMPARE(buffer.pendingBytes(), qsizetype(0));
        QVERIFY(buffer.finish());
    }
}

void TestLineBuffer::reset()
{
    NiriLineBuffer buffer(4);
    buffer.feed("ok\n12345");
    QVERIFY(buffer.error().code != NiriLineBuffer::ErrorCode::None);
    buffer.clear();
    QCOMPARE(buffer.error().code, NiriLineBuffer::ErrorCode::None);
    QCOMPARE(buffer.pendingBytes(), qsizetype(0));
    QCOMPARE(buffer.scannedBytesForTesting(), quint64(0));
    QVERIFY(buffer.takeCompleteLines().isEmpty());
    buffer.feed("new\n");
    QCOMPARE(buffer.takeCompleteLines(), QList<QByteArray>({"new"}));
    buffer.feed("old");
    QVERIFY(!buffer.finish());
    buffer.clear();
    buffer.feed("a\n");
    QCOMPARE(buffer.takeCompleteLines(), QList<QByteArray>({"a"}));
    QVERIFY(buffer.finish());
}

void TestLineBuffer::legacyCompatibility()
{
    NiriLineBuffer buffer;
    QCOMPARE(buffer.maxFrameBytes(), qsizetype(64 * 1024 * 1024));
    buffer.feed("\n \t\r\n  {} \r\n\v\f\npartial");
    QCOMPARE(buffer.takeCompleteLines(), QList<QByteArray>({"{}"}));
    QVERIFY(buffer.takeCompleteLines().isEmpty());
    QCOMPARE(buffer.pendingBytes(), qsizetype(7));
}

void TestLineBuffer::manyFramesWithoutQueue()
{
    NiriLineBuffer buffer(8);
    const QByteArray input = QByteArray("{}\n").repeated(100000);
    int count = 0;
    QVERIFY(buffer.feed(input, [&](const QByteArray &frame) {
        QCOMPARE(frame, QByteArray("{}"));
        ++count;
    }));
    QCOMPARE(count, 100000);
    QCOMPARE(buffer.pendingBytes(), qsizetype(0));
    QVERIFY(buffer.takeCompleteLines().isEmpty());
    QCOMPARE(buffer.scannedBytesForTesting(), quint64(input.size()));
}

QTEST_MAIN(TestLineBuffer)
#include "test_linebuffer.moc"
