#include "nirimock.h"
#include "generated/niriipccodec_generated.h"
#include "generated/niriipcevents_generated.h"
#include "ipcsurfacefixtures.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTest>
#include <limits>

using namespace NiriWire;

class TestMock : public QObject
{
    Q_OBJECT
private:
    static void connectPeer(QLocalSocket &socket, const NiriMock &mock)
    {
        socket.connectToServer(mock.socketPath());
        QVERIFY(socket.waitForConnected(1000));
    }
    static QByteArray receive(QLocalSocket &socket, int lines = 1)
    {
        QByteArray bytes;
        QElapsedTimer timer;
        timer.start();
        while (bytes.count('\n') < lines && timer.elapsed() < 2000) {
            QTest::qWait(5);
            bytes += socket.readAll();
        }
        return bytes;
    }
private slots:
    void socketDirectoryRejectsDeepTmp_data()
    {
        QTest::addColumn<QString>("leaf");
        QTest::newRow("deep") << QString(120, QLatin1Char('d'));
        QTest::newRow("utf8-bytes") << QString(60, QChar(0xe9));
    }
    void socketDirectoryRejectsDeepTmp()
    {
        QFETCH(QString, leaf);
        QTemporaryDir scratch;
        QVERIFY(scratch.isValid());
        const auto deep = scratch.filePath(leaf);
        QVERIFY(QDir().mkpath(deep));
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("TMPDIR", deep);
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(),
                    {"releasedRequests:Version", "-nocrashhandler"});
        QVERIFY(child.waitForFinished(5000));
        QCOMPARE(child.exitStatus(), QProcess::CrashExit);
        QVERIFY((child.readAllStandardOutput() + child.readAllStandardError())
                        .contains("TMPDIR is too long for Unix fixture socket paths"));
        // No fallback directory outside the requested TMPDIR is permitted.
        QCOMPARE(QDir(deep).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size(), 0);
    }

    void socketDirectoryIsShortAndOwned()
    {
        QString path;
        {
            SocketDirectory directory;
            QVERIFY(directory.isValid());
            path = directory.filePath("ipc.sock");
            QCOMPARE(QFileInfo(path).absolutePath().section('/', -1).size(), 9);
            QCOMPARE(QFileInfo(QFileInfo(path).absolutePath()).absolutePath(),
                     QDir(QDir::tempPath()).absolutePath());
            QVERIFY(QFile::encodeName(path).size() < qsizetype(sizeof(sockaddr_un{}.sun_path)));
            QLocalServer server;
            QVERIFY(server.listen(path));
            QLocalSocket peer;
            peer.connectToServer(path);
            QVERIFY(peer.waitForConnected(1000));
        }
        QVERIFY(!QFileInfo::exists(QFileInfo(path).absolutePath()));
    }

    void releasedRequests_data()
    {
        QTest::addColumn<QByteArray>("request");
        QTest::addColumn<QString>("kind");
        for (const auto *name :
             {"Version", "Outputs", "Workspaces", "Windows", "Layers", "KeyboardLayouts",
              "FocusedOutput", "FocusedWindow", "OverviewState", "Casts"})
            QTest::newRow(name) << ('"' + QByteArray(name) + "\"\n") << QString(name);
        QTest::newRow("PickWindow") << QByteArray("\"PickWindow\"\n") << QString("PickedWindow");
        QTest::newRow("PickColor") << QByteArray("\"PickColor\"\n") << QString("PickedColor");
        QTest::newRow("Output") << QByteArray(
                "{\"Output\":{\"output\":\"DP-1\",\"action\":\"On\"}}\n")
                                << QString("OutputConfigChanged");
        QTest::newRow("Action") << QByteArray("{\"Action\":{\"FocusWindow\":{\"id\":7}}}\n")
                                << QString("Handled");
    }
    void releasedRequests()
    {
        QFETCH(QByteArray, request);
        QFETCH(QString, kind);
        NiriMock mock;
        QVERIFY(mock.start());
        QSignalSpy lines(&mock, &NiriMock::requestLine);
        QLocalSocket socket;
        connectPeer(socket, mock);
        socket.write(request);
        const auto response = parse(receive(socket).trimmed());
        QVERIFY(response.has_value());
        QCOMPARE(lines.count(), 1);
        QCOMPARE(lines[0][1].toByteArray() + '\n', request);
        const auto *ok = response->root.lookup("Ok");
        QVERIFY(ok);
        if (kind == "Handled")
            QCOMPARE(*ok->stringValue(), QString("Handled"));
        else {
            QVERIFY(ok->objectValue());
            QCOMPARE(ok->objectValue()->size(), 1);
            QCOMPARE(ok->objectValue()->first().first, kind);
        }
    }
    void interleavedClients()
    {
        NiriMock mock;
        QVERIFY(mock.start());
        QSignalSpy lines(&mock, &NiriMock::requestLine);
        QLocalSocket a, b;
        connectPeer(a, mock);
        connectPeer(b, mock);
        a.write("\"Ver");
        b.write("\"Windows\"\n");
        QCOMPARE(receive(b), QByteArray("{\"Ok\":{\"Windows\":[]}}\n"));
        a.write("sion\"\n");
        const auto version = parse(receive(a).trimmed());
        QVERIFY(version && version->root.lookup("Ok")->lookup("Version"));
        QCOMPARE(lines.count(), 2);
        QVERIFY(lines[0][0] != lines[1][0]);
        QCOMPARE(lines[1][1].toByteArray(), QByteArray("\"Version\""));
        a.write("\"unfin");
        a.abort();
        QTest::qWait(10);
        QLocalSocket c;
        connectPeer(c, mock);
        c.write("{\"Action\":{\"FocusWindow\":{\"id\":7}}}\n");
        QCOMPARE(receive(c), QByteArray("{\"Ok\":\"Handled\"}\n"));
        // A sequence may send the next request without reopening its socket.
        c.write("\"ReturnError\"\n");
        QVERIFY(parse(receive(c).trimmed())->root.lookup("Err"));
    }
    void independentFixtureBytes()
    {
        const auto fixtures =
                NiriSurfaceTest::loadFixture(QFINDTESTDATA("fixtures/serde-v26.04/vectors.json"));
        NiriMock mock;
        QVERIFY(mock.start());
        bool selected = false;
        for (const auto &entry : *fixtures.lookup("surfaceVectors")->arrayValue()) {
            if (*entry.lookup("name")->stringValue() == "response-8") {
                mock.setResponse("FocusedWindow", *entry.lookup("value"));
                selected = true;
            }
        }
        QVERIFY(selected);
        QLocalSocket socket;
        connectPeer(socket, mock);
        socket.write("\"FocusedWindow\"\n");
        const auto bytes = receive(socket);
        QVERIFY(bytes.contains("18446744073709551615"));
        const auto response = parse(bytes.trimmed());
        QCOMPARE(response->root.lookup("Ok")
                         ->lookup("FocusedWindow")
                         ->lookup("id")
                         ->numberValue()
                         ->toUInt64()
                         .value(),
                 std::numeric_limits<quint64>::max());
    }
    void burst_data()
    {
        QTest::addColumn<bool>("layouts");
        QTest::newRow("absent") << false;
        QTest::newRow("present-empty") << true;
    }
    void burst()
    {
        QFETCH(bool, layouts);
        NiriMock mock;
        QVERIFY(mock.start());
        if (layouts)
            mock.setKeyboardLayouts(parse("{\"names\":[],\"current_idx\":0}")->root);
        QLocalSocket a, b;
        connectPeer(a, mock);
        connectPeer(b, mock);
        a.write("\"EventStream\"\n");
        b.write("\"EventStream\"\n");
        const auto bytes = receive(a, layouts ? 7 : 6);
        QCOMPARE(receive(b, layouts ? 7 : 6), bytes);
        const auto rows = bytes.trimmed().split('\n');
        QCOMPARE(rows[0], QByteArray("{\"Ok\":\"Handled\"}"));
        QStringList expected{"WorkspacesChanged", "WindowsChanged"};
        if (layouts)
            expected << "KeyboardLayoutsChanged";
        expected << "OverviewOpenedOrClosed" << "ConfigLoaded" << "CastsChanged";
        QCOMPARE(rows.size(), expected.size() + 1);
        for (int i = 1; i < rows.size(); ++i) {
            const auto document = parse(rows[i]);
            QCOMPARE(document->root.objectValue()->first().first, expected[i - 1]);
            NiriEvent event;
            NiriIpc::DecodeError error;
            QVERIFY(NiriIpc::decodeEvent(document->root, event, error));
        }
        QCOMPARE(parse(rows[1])
                         ->root.lookup("WorkspacesChanged")
                         ->lookup("workspaces")
                         ->arrayValue()
                         ->size(),
                 0);
        QCOMPARE(parse(rows[2])
                         ->root.lookup("WindowsChanged")
                         ->lookup("windows")
                         ->arrayValue()
                         ->size(),
                 0);
        QCOMPARE(mock.streamCount(), 2);
        a.write("\"Version\"\n");
        QTest::qWait(30);
        QCOMPARE(a.bytesAvailable(), 0); // Stream mode never answers another request.
        a.abort();
        QTRY_COMPARE(mock.streamCount(), 1);
        b.abort();
        QTRY_COMPARE(mock.streamCount(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        mock.sendEventBytes("{\"ConfigLoaded\":{\"failed\":false}}\n");
    }
    void connectedBroadcast_data()
    {
        QTest::addColumn<int>("delayMs");
        QTest::newRow("queued") << 0;
        QTest::newRow("delayed-fragmented") << 100;
    }
    void connectedBroadcast()
    {
        QFETCH(int, delayMs);
        NiriMock mock;
        QVERIFY(mock.start());
        NiriMock::Fault fault;
        fault.delayMs = delayMs;
        fault.fragments = {1, 2, 3};
        mock.enqueueFault(fault);
        QSignalSpy connected(&mock, &NiriMock::connected);
        QSignalSpy requests(&mock, &NiriMock::requestLine);
        const QByteArray event = "{\"ConfigLoaded\":{\"failed\":true}}\n";
        connect(&mock, &NiriMock::connected, &mock, [&] { mock.sendEventBytes(event); });
        QLocalSocket socket;
        connectPeer(socket, mock);
        socket.write("\"EventStream\"\n\"Version\"\n");
        if (delayMs) {
            QTRY_COMPARE(requests.count(), 1);
            QCOMPARE(connected.count(), 0);
            QCOMPARE(mock.streamCount(), 0);
            mock.sendEventBytes("{\"WindowClosed\":{\"id\":7}}\n");
            QTest::qWait(20);
            QCOMPARE(socket.bytesAvailable(), 0);
        }
        const auto rows = receive(socket, 7).trimmed().split('\n');
        QCOMPARE(rows.size(), 7);
        QCOMPARE(rows.first(), QByteArray("{\"Ok\":\"Handled\"}"));
        QCOMPARE(rows[5], QByteArray("{\"CastsChanged\":{\"casts\":[]}}"));
        QCOMPARE(rows.last() + '\n', event);
        QCOMPARE(connected.count(), 1);
        QCOMPARE(mock.streamCount(), 1);
        QCOMPARE(requests.count(), 1); // Suppress input while the subscription is pending too.
    }
    void inactiveStream_data()
    {
        QTest::addColumn<bool>("drop");
        QTest::newRow("dropped") << true;
        QTest::newRow("closed") << false;
    }
    void inactiveStream()
    {
        QFETCH(bool, drop);
        NiriMock mock;
        QVERIFY(mock.start());
        NiriMock::Fault fault;
        fault.drop = drop;
        fault.close = !drop;
        mock.enqueueFault(fault);
        QSignalSpy connected(&mock, &NiriMock::connected);
        QSignalSpy disconnected(&mock, &NiriMock::disconnected);
        QLocalSocket socket;
        connectPeer(socket, mock);
        socket.write("\"EventStream\"\n");
        QTest::qWait(100);
        QCOMPARE(connected.count(), 0);
        QCOMPARE(disconnected.count(), 0);
        QCOMPARE(mock.streamCount(), 0);
        if (drop) {
            mock.sendEventBytes("{\"ConfigLoaded\":{\"failed\":true}}\n");
            QTest::qWait(20);
            QCOMPARE(socket.bytesAvailable(), 0);
        } else {
            QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        }
    }
    void faults()
    {
        NiriMock mock;
        QVERIFY(mock.start());
        QLocalSocket socket;
        connectPeer(socket, mock);
        NiriMock::Fault delay;
        delay.delayMs = 80;
        delay.fragments = {1, 2, 3};
        mock.enqueueFault(delay);
        socket.write("\"Version\"\n");
        QTest::qWait(30);
        QCOMPARE(socket.bytesAvailable(), 0);
        QVERIFY(parse(receive(socket).trimmed()));
        NiriMock::Fault drop;
        drop.drop = true;
        mock.enqueueFault(drop);
        socket.write("\"Version\"\n");
        QTest::qWait(100);
        QCOMPARE(socket.bytesAvailable(), 0);
        for (const auto &bytes : {QByteArray("bad json\n"), QByteArray(100000, 'x') + '\n',
                                  QByteArray("{\"Err\":\"injected\"}\n")}) {
            NiriMock::Fault replace;
            replace.bytes = bytes;
            mock.enqueueFault(replace);
            socket.write("\"Version\"\n");
            QCOMPARE(receive(socket), bytes);
        }
    }
};
QTEST_GUILESS_MAIN(TestMock)
#include "test_mock.moc"
