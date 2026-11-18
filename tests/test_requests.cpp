#include "niriactions.h"
#include "nirirequests.h"
#include "nirirequestsequence.h"
#include <QDir>
#include <QJsonDocument>
#include <QLocalServer>
#include <QPointer>
#include <QProcess>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QMetaMethod>
#include <limits>

using namespace NiriWire;

class RequestServer
{
public:
    RequestServer()
    {
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            while (auto *peer = server.nextPendingConnection()) {
                ++connections;
                peers.append(peer);
                auto framer = std::make_shared<NiriLineBuffer>();
                QObject::connect(peer, &QLocalSocket::readyRead, &server, [this, peer, framer] {
                    framer->feed(peer->readAll(), [this, peer](const QByteArray &line) {
                        arrivals.append(line);
                        if (handler)
                            handler(peer, line);
                        else
                            peer->write(response);
                    });
                });
            }
        });
    }
    bool listen() { return directory.isValid() && server.listen(directory.filePath("socket")); }
    QString path() const { return server.fullServerName(); }
    QByteArray response = "{\"Ok\":\"Handled\"}\n";
    std::function<void(QLocalSocket *, const QByteArray &)> handler;
    QList<QByteArray> arrivals;
    QList<QPointer<QLocalSocket>> peers;
    int connections = 0;
    QTemporaryDir directory;
    QLocalServer server;
};

class TestRequests : public QObject
{
    Q_OBJECT
    static bool invokeTypedQuery(const QString &name, NiriPendingReply *&reply)
    {
        auto *requests = NiriRequests::instance();
        const auto signature = name.toLatin1() + "()";
        const auto index = requests->metaObject()->indexOfMethod(signature.constData());
        if (index < 0)
            return false;
        const auto method = requests->metaObject()->method(index);
        // Qt requires the exact declared return type, even for QObject pointers.
        // Allocate correctly typed storage, then use Qt's safe pointer conversion.
        QVariant result(method.returnMetaType());
        if (!method.invoke(requests, QGenericReturnArgument(method.typeName(), result.data())))
            return false;
        reply = result.value<NiriPendingReply *>();
        return reply != nullptr;
    }
private slots:
    void captureScopeReentrancy_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("reply") << QString("reply");
        QTest::newRow("sequence") << QString("sequence");
    }

    void captureScopeReentrancy()
    {
        QFETCH(QString, kind);
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("NIRI_SOCKET", "/niriqml-missing-capture-scope");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {"--capture-scope", kind});
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
    }

    void init()
    {
        NiriSession::instance()->stop();
        NiriSession::instance()->selectEndpoint(QString());
        NiriRequests::instance()->setRequestTimeoutMs(5000);
        NiriRequests::instance()->setMaxFrameBytes(NiriLineBuffer::DefaultMaxFrameBytes);
    }

    void qmlPostReturn_data()
    {
        QTest::addColumn<QString>("mode");
        for (auto mode : {"empty", "missing", "refused", "invalid-json", "invalid-option", "valid"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }

    void qmlPostReturn()
    {
        QFETCH(QString, mode);
        RequestServer server;
        QVERIFY(server.listen());
        server.response = "{\"Ok\":{\"Version\":\"26.04\"}}\n";
        if (mode == "valid")
            NiriSession::instance()->selectEndpoint(server.path());
        else if (mode == "missing")
            NiriSession::instance()->selectEndpoint(server.directory.filePath("missing"));
        else if (mode == "refused") {
            auto path = server.path();
            server.server.close();
            NiriSession::instance()->selectEndpoint(path);
        }
        QQmlEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        const auto call = mode == "invalid-json" ? "NiriRequests.sendRawJson('{')"
                : mode == "invalid-option" ? "NiriRequests.sendRaw('Version', {timeoutMs: -2})"
                                           : "NiriRequests.version()";
        component.setData(
                QByteArray("import QtQml\nimport Niri 1.0\nQtObject { id: root; "
                           "property NiriPendingReply retained: null; property int count: 0; "
                           "property bool inlineFinish: false; property bool coherent: false; "
                           "Component.onCompleted: { let r = ")
                        + call
                        + "; retained = r; inlineFinish = r.completed; "
                          "r.finished.connect(function() { root.count++; "
                          "root.coherent = r.completed && r.status !== NiriPendingReply.Pending "
                          "&& (r.isError ? r.error.category.length > 0 : r.value === '26.04'); }); "
                          "} }",
                QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QVERIFY(!root->property("inlineFinish").toBool());
        QTRY_COMPARE(root->property("count").toInt(), 1);
        QVERIFY(root->property("coherent").toBool());
        auto *reply = root->property("retained").value<NiriPendingReply *>();
        QVERIFY(reply);
        QVERIFY(!reply->parent());
        QCOMPARE(QJSEngine::objectOwnership(reply), QJSEngine::JavaScriptOwnership);
        QCOMPARE(reply->isError(), mode != "valid");
        engine.collectGarbage();
        QTest::qWait(20);
        QCOMPARE(root->property("count").toInt(), 1);
        QVERIFY(reply->completed());
    }

    void envelopes_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("category");
        QTest::newRow("handled") << QByteArray("{\"Ok\":\"Handled\"}\n") << QString();
        QTest::newRow("unknown") << QByteArray("{\"Ok\":{\"Future\":18446744073709551615}}\n")
                                 << QString();
        QTest::newRow("server") << QByteArray("{\"Err\":\"denied\"}\n") << QString("Server");
        QTest::newRow("two") << QByteArray("{\"Ok\":\"Handled\",\"Err\":\"no\"}\n")
                             << QString("Protocol");
        QTest::newRow("extra") << QByteArray("{\"Ok\":null,\"extra\":1}\n") << QString("Protocol");
        QTest::newRow("err-type") << QByteArray("{\"Err\":42}\n") << QString("Protocol");
        QTest::newRow("neither") << QByteArray("{}\n") << QString("Protocol");
        QTest::newRow("scalar") << QByteArray("true\n") << QString("Protocol");
        QTest::newRow("parse") << QByteArray("{broken}\n") << QString("Parse");
        QTest::newRow("duplicate") << QByteArray("{\"Ok\":1,\"Ok\":2}\n") << QString("Parse");
        // Released Response variants; raw preserves all payload kinds, including
        // option nulls, without requiring the typed schema to recognize them.
        for (auto payload :
             {"{\"Version\":\"26.04\"}", "{\"Outputs\":{}}", "{\"Workspaces\":[]}",
              "{\"Windows\":[]}", "{\"Layers\":[]}",
              "{\"KeyboardLayouts\":{\"names\":[],\"current_idx\":0}}", "{\"FocusedOutput\":null}",
              "{\"FocusedWindow\":null}", "{\"PickedWindow\":null}", "{\"PickedColor\":null}",
              "{\"OutputConfigChanged\":\"Applied\"}", "{\"OverviewState\":{\"is_open\":false}}",
              "{\"Casts\":[]}"})
            QTest::newRow(payload) << QByteArray("{\"Ok\":") + payload + "}\n" << QString();
    }

    void envelopes()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, category);
        RequestServer server;
        QVERIFY(server.listen());
        server.response = bytes;
        NiriSession::instance()->selectEndpoint(server.path());
        std::unique_ptr<NiriPendingReply> reply(
                NiriRequests::instance()->sendRaw(QVariant("Version")));
        QSignalSpy finished(reply.get(), &NiriPendingReply::finished);
        QVERIFY(!reply->completed());
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(reply->error().category, category);
        QCOMPARE(reply->executionCertainty(), QString("ReplyReceived"));
        QCOMPARE(reply->json().toUtf8(), bytes.chopped(1));
        if (bytes.contains("Future")) {
            QCOMPARE(reply->value().toMap().value("Future").toString(),
                     QString("18446744073709551615"));
            QCOMPARE(reply->responseKind(), QString("Future"));
        }
        if (bytes == "{\"Ok\":\"Handled\"}\n") {
            QCOMPARE(reply->value().toString(), QString("Handled"));
            QCOMPARE(reply->document().toMap().value("Ok").toString(), QString("Handled"));
            QCOMPARE(reply->responseKind(), QString("Handled"));
        }
        QTest::qWait(10);
        QCOMPARE(finished.count(), 1);
    }

    void typedDiscriminator_data()
    {
        QTest::addColumn<QString>("method");
        for (auto method : {"windows", "workspaces", "outputs", "focusedWindow", "focusedOutput",
                            "keyboardLayouts", "version"})
            QTest::newRow(method) << QString::fromLatin1(method);
    }

    void typedWrongShape_data() { typedDiscriminator_data(); }

    void typedWrongShape()
    {
        QFETCH(QString, method);
        RequestServer server;
        QVERIFY(server.listen());
        QString kind = method;
        kind[0] = kind[0].toUpper();
        server.response = "{\"Ok\":{\"" + kind.toUtf8() + "\":false}}\n";
        NiriSession::instance()->selectEndpoint(server.path());
        NiriPendingReply *raw = nullptr;
        QVERIFY(invokeTypedQuery(method, raw));
        std::unique_ptr<NiriPendingReply> reply(raw);
        QTRY_VERIFY(reply->completed());
        QCOMPARE(reply->error().category, QString("Decode"));
    }

    void legacyProjectionFailureIsDecode()
    {
        RequestServer server;
        QVERIFY(server.listen());
        server.response = "{\"Ok\":{\"FocusedWindow\":{\"id\":1e999}}}\n";
        NiriSession::instance()->selectEndpoint(server.path());
        std::unique_ptr<NiriPendingReply> reply(NiriRequests::instance()->focusedWindow());
        QTRY_VERIFY(reply->completed());
        QCOMPARE(reply->error().category, QString("Decode"));
        QCOMPARE(reply->json().toUtf8(), server.response.chopped(1));
    }

    void qmlSequenceAndExactNumber()
    {
        RequestServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.path());
        QQmlEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import Niri 1.0
            QtObject {
                id: root
                property NiriRequestSequence lane: null
                property NiriPendingReply first: null
                property NiriPendingReply second: null
                property int count: 0
                Component.onCompleted: {
                    lane = NiriRequests.sequence();
                    first = lane.request("Version");
                    second = lane.action({Future: {id: NiriRequests.number("18446744073709551615")}});
                    first.finished.connect(function() { root.count++; });
                    second.finished.connect(function() { root.count++; });
                }
            }
        )",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QTRY_COMPARE(root->property("count").toInt(), 2);
        QCOMPARE(server.connections, 1);
        QCOMPARE(server.arrivals,
                 QList<QByteArray>({"\"Version\"",
                                    "{\"Action\":{\"Future\":{\"id\":18446744073709551615}}}"}));
    }

    void qmlGarbageCollection()
    {
        RequestServer server;
        QVERIFY(server.listen());
        server.response.clear();
        NiriSession::instance()->selectEndpoint(server.path());
        QQmlEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import Niri 1.0
            QtObject {
                property NiriPendingReply retained: null
                Component.onCompleted: retained = NiriRequests.version()
                function drop() { retained = null; }
            }
        )",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QPointer<NiriPendingReply> reply = root->property("retained").value<NiriPendingReply *>();
        QVERIFY(reply);
        QTRY_COMPARE(server.arrivals.size(), 1);
        engine.collectGarbage();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(reply);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "drop"));
        engine.collectGarbage();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTRY_VERIFY(reply.isNull());
        QTRY_COMPARE(server.peers.first()->state(), QLocalSocket::UnconnectedState);
    }

    void qmlPendingEngineTeardown()
    {
        RequestServer server;
        QVERIFY(server.listen());
        server.response.clear();
        NiriSession::instance()->selectEndpoint(server.path());
        auto engine = std::make_unique<QQmlEngine>();
        engine->addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(engine.get());
        component.setData(R"(
            import QtQml
            import Niri 1.0
            QtObject {
                property NiriRequestSequence lane: NiriRequests.sequence()
                property NiriPendingReply pending: lane.request("Version")
            }
        )",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QPointer<NiriPendingReply> reply = root->property("pending").value<NiriPendingReply *>();
        QPointer<NiriRequestSequence> lane = root->property("lane").value<NiriRequestSequence *>();
        QTRY_COMPARE(server.arrivals.size(), 1);
        root.reset();
        engine.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(reply.isNull());
        QVERIFY(lane.isNull());
        QTRY_COMPARE(server.peers.first()->state(), QLocalSocket::UnconnectedState);
    }

    void sequenceIdleDisconnectDoesNotReconnect()
    {
        RequestServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.path());
        std::unique_ptr<NiriRequestSequence> sequence(NiriRequests::instance()->sequence());
        std::unique_ptr<NiriPendingReply> first(sequence->request("First"));
        QTRY_VERIFY(first->completed());
        server.peers.first()->disconnectFromServer();
        QTRY_VERIFY(sequence->stopped());
        std::unique_ptr<NiriPendingReply> second(sequence->request("Second"));
        QTRY_VERIFY(second->completed());
        QVERIFY(second->isError());
        QCOMPARE(second->executionCertainty(), QString("NotSent"));
        QCOMPARE(server.connections, 1);
        QCOMPARE(server.arrivals.size(), 1);
    }

    void sequenceExtraData_data()
    {
        QTest::addColumn<QByteArray>("tail");
        QTest::newRow("extra-reply") << QByteArray("{\"Ok\":\"Handled\"}\n");
        QTest::newRow("partial-reply") << QByteArray("{\"Ok\":");
        QTest::newRow("oversized-tail") << QByteArray(33, 'x');
    }

    void sequenceExtraData()
    {
        QFETCH(QByteArray, tail);
        RequestServer server;
        QVERIFY(server.listen());
        server.response += tail;
        NiriSession::instance()->selectEndpoint(server.path());
        NiriRequests::instance()->setMaxFrameBytes(32);
        std::unique_ptr<NiriRequestSequence> sequence(NiriRequests::instance()->sequence());
        std::unique_ptr<NiriPendingReply> first(sequence->request("First"));
        std::unique_ptr<NiriPendingReply> second(sequence->request("Second"));
        QTRY_VERIFY(first->completed());
        QTRY_VERIFY(second->completed());
        QVERIFY(!first->isError());
        QVERIFY(second->isError());
        QCOMPARE(second->executionCertainty(), QString("NotSent"));
        QCOMPARE(server.arrivals.size(), 1);
    }

    void rawRoots_data()
    {
        QTest::addColumn<QVariant>("input");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("null") << QVariant::fromValue(nullptr) << QByteArray("null");
        QTest::newRow("true") << QVariant(true) << QByteArray("true");
        QTest::newRow("false") << QVariant(false) << QByteArray("false");
        QTest::newRow("integer") << QVariant(42) << QByteArray("42");
        QTest::newRow("decimal") << QVariant(1.25) << QByteArray("1.25");
        QTest::newRow("array") << QVariant(QVariantList{1, "x", false})
                               << QByteArray("[1,\"x\",false]");
    }

    void rawRoots()
    {
        QFETCH(QVariant, input);
        QFETCH(QByteArray, expected);
        RequestServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.path());
        std::unique_ptr<NiriPendingReply> reply(NiriRequests::instance()->sendRaw(input));
        QTRY_VERIFY(reply->completed());
        QVERIFY(!reply->isError());
        QCOMPARE(server.arrivals, QList<QByteArray>{expected});
    }

    void typedDiscriminator()
    {
        QFETCH(QString, method);
        RequestServer server;
        QVERIFY(server.listen());
        server.response = "{\"Ok\":{\"Wrong\":null}}\n";
        NiriSession::instance()->selectEndpoint(server.path());
        NiriPendingReply *raw = nullptr;
        QVERIFY(invokeTypedQuery(method, raw));
        std::unique_ptr<NiriPendingReply> reply(raw);
        QTRY_VERIFY(reply->completed());
        QCOMPARE(reply->error().category, QString("Decode"));
        QVERIFY(reply->error().fieldPath.startsWith("$.Ok."));
        QCOMPARE(reply->json().toUtf8(), server.response.chopped(1));
    }

    void typedNullAndEmpty_data()
    {
        QTest::addColumn<QString>("method");
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("windows") << QString("windows") << QByteArray("{\"Ok\":{\"Windows\":[]}}\n");
        QTest::newRow("workspaces")
                << QString("workspaces") << QByteArray("{\"Ok\":{\"Workspaces\":[]}}\n");
        QTest::newRow("outputs") << QString("outputs") << QByteArray("{\"Ok\":{\"Outputs\":{}}}\n");
        QTest::newRow("window-null")
                << QString("focusedWindow") << QByteArray("{\"Ok\":{\"FocusedWindow\":null}}\n");
        QTest::newRow("output-null")
                << QString("focusedOutput") << QByteArray("{\"Ok\":{\"FocusedOutput\":null}}\n");
        QTest::newRow("layouts")
                << QString("keyboardLayouts")
                << QByteArray("{\"Ok\":{\"KeyboardLayouts\":{\"names\":[],\"current_idx\":0}}}\n");
        QTest::newRow("version") << QString("version")
                                 << QByteArray("{\"Ok\":{\"Version\":\"\"}}\n");
    }

    void typedNullAndEmpty()
    {
        QFETCH(QString, method);
        QFETCH(QByteArray, response);
        RequestServer server;
        QVERIFY(server.listen());
        server.response = response;
        NiriSession::instance()->selectEndpoint(server.path());
        NiriPendingReply *raw = nullptr;
        QVERIFY(invokeTypedQuery(method, raw));
        std::unique_ptr<NiriPendingReply> reply(raw);
        QTRY_VERIFY(reply->completed());
        QVERIFY2(!reply->isError(), qPrintable(reply->error().message));
        if (method.startsWith("focused"))
            QCOMPARE(reply->value().metaType(), QMetaType::fromType<std::nullptr_t>());
    }

    void cancellationRaces_data()
    {
        QTest::addColumn<QString>("mode");
        for (auto mode : {"before-start", "after-send", "timeout", "reply-first", "cancel-first",
                          "error", "endpoint"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }

    void cancellationRaces()
    {
        QFETCH(QString, mode);
        RequestServer server;
        QVERIFY(server.listen());
        server.response.clear();
        NiriSession::instance()->selectEndpoint(server.path());
        std::unique_ptr<NiriPendingReply> reply(NiriRequests::instance()->sendRaw(
                QVariant("Version"), {{"timeoutMs", mode == "timeout" ? 30 : 5000}}));
        QSignalSpy finished(reply.get(), &NiriPendingReply::finished);
        if (mode == "before-start") {
            reply->cancel();
            QVERIFY(!reply->completed());
        } else {
            QTRY_COMPARE(server.arrivals.size(), 1);
            if (mode == "reply-first") {
                server.peers.first()->write("{\"Ok\":\"Handled\"}\n");
                QTRY_VERIFY(reply->completed());
                reply->cancel();
            } else if (mode == "cancel-first") {
                reply->cancel();
                server.peers.first()->write("{\"Ok\":\"Handled\"}\n");
            } else if (mode == "after-send") {
                reply->cancel();
            } else if (mode == "error") {
                server.peers.first()->disconnectFromServer();
            } else if (mode == "endpoint") {
                NiriSession::instance()->selectEndpoint(server.directory.filePath("other"));
            }
        }
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(reply->executionCertainty(),
                 mode == "before-start"          ? QString("NotSent")
                         : mode == "reply-first" ? QString("ReplyReceived")
                                                 : QString("MayHaveExecuted"));
        const auto category = mode == "timeout" ? "Timeout"
                : mode == "error"               ? "Transport"
                : mode == "endpoint"            ? "EndpointChanged"
                : mode == "reply-first"         ? ""
                                                : "Cancelled";
        QCOMPARE(reply->error().category, QString::fromLatin1(category));
        QCOMPARE(reply->endpoint(), server.path());
        reply->cancel();
        QTest::qWait(40);
        QCOMPARE(finished.count(), 1);
    }

    void deletion_data()
    {
        QTest::addColumn<QString>("mode");
        for (auto mode : {"before-start", "before-reply", "before-timeout", "from-finished"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }

    void deletion()
    {
        QFETCH(QString, mode);
        RequestServer server;
        QVERIFY(server.listen());
        server.response.clear();
        NiriSession::instance()->selectEndpoint(server.path());
        auto *reply = NiriRequests::instance()->sendRaw(QVariant("Version"), {{"timeoutMs", 80}});
        QPointer<NiriPendingReply> guard(reply);
        int completions = 0;
        QObject observer;
        connect(reply, &NiriPendingReply::finished, &observer, [&] {
            ++completions;
            delete reply;
        });
        if (mode != "before-start")
            QTRY_COMPARE(server.arrivals.size(), 1);
        if (mode != "from-finished")
            delete reply;
        if (mode == "before-reply" || mode == "from-finished")
            server.peers.first()->write("{\"Ok\":\"Handled\"}\n");
        QTest::qWait(120);
        QVERIFY(guard.isNull());
        QCOMPARE(completions, mode == "from-finished" ? 1 : 0);
    }

    void framingEof_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("category");
        QTest::newRow("clean") << QByteArray() << QString("Transport");
        QTest::newRow("partial") << QByteArray("{\"Ok\":\"Handled\"}") << QString("Framing");
        QTest::newRow("complete") << QByteArray("{\"Ok\":\"Handled\"}\n") << QString();
    }
    void framingEof()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, category);
        RequestServer server;
        QVERIFY(server.listen());
        server.handler = [bytes](auto *peer, const auto &) {
            peer->write(bytes);
            peer->disconnectFromServer();
        };
        NiriSession::instance()->selectEndpoint(server.path());
        std::unique_ptr<NiriPendingReply> reply(
                NiriRequests::instance()->sendRaw(QVariant("Version")));
        QTRY_VERIFY(reply->completed());
        QCOMPARE(reply->error().category, category);
    }

    void exactOutbound()
    {
        RequestServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.path());
        const QString text =
                QStringLiteral("quote\"\\\n\r") + QChar(0) + QString::fromUtf8("snow \xe2\x98\x83");
        std::unique_ptr<NiriPendingReply> escaped(
                NiriRequests::instance()->sendRaw(QVariant(text)));
        QTRY_VERIFY(escaped->completed());
        QCOMPARE(server.arrivals.size(), 1);
        auto parsed = parse(server.arrivals.first());
        QVERIFY(parsed);
        QCOMPARE(*parsed->root.stringValue(), text);
        for (const auto id :
             {quint64(0), quint64(9007199254740993ULL), quint64(9223372036854775808ULL),
              std::numeric_limits<quint64>::max()}) {
            std::unique_ptr<NiriPendingReply> reply(NiriActions::instance()->focusWindow(id));
            QTRY_VERIFY(reply->completed());
            QCOMPARE(server.arrivals.last(),
                     QByteArray("{\"Action\":{\"FocusWindow\":{\"id\":") + QByteArray::number(id)
                             + "}}}");
        }
        auto exact = QStringLiteral("{\"Action\":{\"Future\":{\"id\":18446744073709551615}}}");
        std::unique_ptr<NiriPendingReply> raw(NiriRequests::instance()->sendRawJson(exact));
        QTRY_VERIFY(raw->completed());
        QCOMPARE(server.arrivals.last(), exact.toUtf8());
        QVariantMap input{{"id",
                           QVariant::fromValue(
                                   NumberToken::fromUInt64(std::numeric_limits<quint64>::max()))}};
        std::unique_ptr<NiriPendingReply> wrapper(
                NiriRequests::instance()->sendRaw(QVariant(input)));
        QTRY_VERIFY(wrapper->completed());
        QCOMPARE(server.arrivals.last(), QByteArray("{\"id\":18446744073709551615}"));
    }

    void conversionFailure_data()
    {
        QTest::addColumn<QString>("method");
        for (auto method : {"action", "width", "height", "raw-variant", "raw-object", "raw-json",
                            "sequence-request", "sequence-action"})
            QTest::newRow(method) << QString::fromLatin1(method);
    }

    void conversionFailure()
    {
        QFETCH(QString, method);
        RequestServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.path());
        QJsonValue nested(true);
        for (int depth = 0; depth < 129; ++depth)
            nested = QJsonObject{{"nested", nested}};
        const auto object = nested.toObject();
        const auto input = object.toVariantMap();
        std::unique_ptr<NiriRequestSequence> sequence;
        NiriPendingReply *raw = nullptr;
        if (method == "action")
            raw = NiriActions::instance()->sendAction(object);
        else if (method == "width")
            raw = NiriActions::instance()->setWindowWidth(42, input);
        else if (method == "height")
            raw = NiriActions::instance()->setWindowHeight(42, input);
        else if (method == "raw-variant")
            raw = NiriRequests::instance()->sendRaw(QVariant(input));
        else if (method == "raw-object")
            raw = NiriRequests::instance()->sendRaw(object);
        else if (method == "raw-json")
            raw = NiriRequests::instance()->sendRawJson(
                    QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
        else {
            sequence.reset(NiriRequests::instance()->sequence());
            raw = method == "sequence-request" ? sequence->request(input) : sequence->action(input);
        }
        std::unique_ptr<NiriPendingReply> reply(raw);
        QVERIFY(reply);
        QSignalSpy finished(reply.get(), &NiriPendingReply::finished);
        QVERIFY(!reply->completed());
        QCOMPARE(finished.count(), 0);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(reply->error().category, QString("Configuration"));
        QVERIFY(!reply->error().message.isEmpty());
        QCOMPARE(reply->executionCertainty(), QString("NotSent"));
        QCOMPARE(reply->endpoint(), server.path());
        reply->cancel();
        QTest::qWait(20);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(server.connections, 0);
        QVERIFY(server.arrivals.isEmpty());
    }

    void legacyConversionFailure()
    {
        RequestServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.path());
        QJsonValue nested(true);
        for (int depth = 0; depth < 129; ++depth)
            nested = QJsonObject{{"nested", nested}};
        QObject observer;
        int completions = 0;
        bool succeeded = true;
        QJsonObject result;
        QPointer<QObject> guard(&observer);
        NiriRequests::instance()->sendJson(
                nested,
                [guard, &completions, &succeeded, &result](bool ok, const QJsonObject &value) {
                    if (!guard)
                        return;
                    ++completions;
                    succeeded = ok;
                    result = value;
                });
        QCOMPARE(completions, 0);
        QTRY_COMPARE(completions, 1);
        QVERIFY(!succeeded);
        QVERIFY(!result.value("error").toString().isEmpty());
        QTest::qWait(20);
        QCOMPARE(completions, 1);
        QCOMPARE(server.connections, 0);
    }

    void actionHelperIds_data()
    {
        QTest::addColumn<QString>("method");
        for (auto method :
             {"focusWindow", "closeWindow", "moveWindowToWorkspace", "moveWindowToMonitor",
              "setWindowWidth", "setWindowHeight", "toggleWindowFloating"})
            QTest::newRow(method) << QString::fromLatin1(method);
    }

    void actionHelperIds()
    {
        QFETCH(QString, method);
        RequestServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.path());
        auto *actions = NiriActions::instance();
        const auto id = std::numeric_limits<quint64>::max();
        NiriPendingReply *raw = nullptr;
        if (method == "focusWindow")
            raw = actions->focusWindow(id);
        else if (method == "closeWindow")
            raw = actions->closeWindow(id);
        else if (method == "moveWindowToWorkspace")
            raw = actions->moveWindowToWorkspace(id, QString("main"), true);
        else if (method == "moveWindowToMonitor")
            raw = actions->moveWindowToMonitor(id, "DP-2");
        else if (method == "setWindowWidth")
            raw = actions->setWindowWidth(id, 800);
        else if (method == "setWindowHeight")
            raw = actions->setWindowHeight(id, QVariantMap{{"AdjustFixed", 20}});
        else
            raw = actions->toggleWindowFloating(id);
        std::unique_ptr<NiriPendingReply> reply(raw);
        QVERIFY(reply);
        QVERIFY(!reply->completed());
        QTRY_VERIFY(reply->completed());
        QVERIFY2(!reply->isError(), qPrintable(reply->error().message));
        QCOMPARE(server.arrivals.size(), 1);
        const auto document = parse(server.arrivals.first());
        QVERIFY(document);
        QString kind = method;
        kind[0] = kind[0].toUpper();
        const auto *action = document->root.lookup("Action");
        QVERIFY(action);
        const auto *body = action->lookup(kind);
        QVERIFY(body);
        const auto *value = body->lookup(method == "moveWindowToWorkspace" ? "window_id" : "id");
        QVERIFY(value && value->numberValue());
        QCOMPARE(value->numberValue()->toUInt64(), std::optional<quint64>(id));
    }

    void correctedActionKeys()
    {
        RequestServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.path());
        QList<NiriPendingReply *> replies{
                NiriActions::instance()->moveWindowToMonitor(42, "DP-2"),
                NiriActions::instance()->setWindowWidth(42, 800),
                NiriActions::instance()->setWindowHeight(42, QVariantMap{{"AdjustFixed", 20}})};
        QTRY_COMPARE(server.arrivals.size(), 3);
        for (const auto &line : server.arrivals) {
            auto doc = parse(line);
            QVERIFY(doc);
            const auto &body = doc->root.lookup("Action")->objectValue()->first().second;
            QVERIFY(body.lookup("id"));
            QVERIFY(!body.lookup("window_id"));
            QVERIFY(!body.lookup("focus"));
            QCOMPARE(body.lookup("id")->numberValue()->toUInt64().value(), quint64(42));
        }
        for (auto *reply : replies) {
            QTRY_VERIFY(reply->completed());
            delete reply;
        }
    }

    void sequenceOrderAndIndependence()
    {
        RequestServer server;
        QVERIFY(server.listen());
        server.response.clear();
        NiriSession::instance()->selectEndpoint(server.path());
        std::unique_ptr<NiriRequestSequence> sequence(NiriRequests::instance()->sequence());
        std::unique_ptr<NiriPendingReply> first(sequence->request("First"));
        std::unique_ptr<NiriPendingReply> second(
                sequence->action(QVariantMap{{"Second", QVariantMap()}}));
        QTRY_COMPARE(server.arrivals.size(), 1);
        QCOMPARE(server.arrivals.first(), QByteArray("\"First\""));
        std::unique_ptr<NiriPendingReply> independent(
                NiriRequests::instance()->sendRaw(QVariant("Independent")));
        QTRY_COMPARE(server.arrivals.size(), 2);
        QCOMPARE(server.arrivals.last(), QByteArray("\"Independent\""));
        auto *sequencePeer = server.peers.first().data();
        sequencePeer->write("{\"Ok\":\"Handled\"}\n");
        QTRY_COMPARE(server.arrivals.size(), 3);
        QCOMPARE(server.arrivals.last(), QByteArray("{\"Action\":{\"Second\":{}}}"));
        QCOMPARE(server.connections, 2);
        sequencePeer->write("{\"Ok\":\"Handled\"}\n");
        QTRY_VERIFY(second->completed());
        QVERIFY(!first->isError());
        QVERIFY(!second->isError());
        QVERIFY(!sequence->stopped());
        independent->cancel();
    }

    void sequenceFailure_data()
    {
        QTest::addColumn<QString>("mode");
        for (auto mode : {"server", "timeout", "cancel", "transport", "endpoint", "delete-entry",
                          "delete-sequence"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }

    void sequenceFailure()
    {
        QFETCH(QString, mode);
        RequestServer server;
        QVERIFY(server.listen());
        server.response.clear();
        NiriSession::instance()->selectEndpoint(server.path());
        std::unique_ptr<NiriRequestSequence> sequence(NiriRequests::instance()->sequence());
        std::unique_ptr<NiriPendingReply> first(
                sequence->request("First", {{"timeoutMs", mode == "timeout" ? 40 : 5000}}));
        std::unique_ptr<NiriPendingReply> second(sequence->request("Second"));
        std::unique_ptr<NiriPendingReply> third(sequence->request("Third"));
        QTRY_COMPARE(server.arrivals.size(), 1);
        if (mode == "server")
            server.peers.first()->write("{\"Err\":\"no\"}\n");
        else if (mode == "transport")
            server.peers.first()->disconnectFromServer();
        else if (mode == "cancel")
            first->cancel();
        else if (mode == "endpoint")
            NiriSession::instance()->selectEndpoint(server.directory.filePath("other"));
        else if (mode == "delete-entry")
            first.reset();
        else if (mode == "delete-sequence")
            sequence.reset();
        QTRY_VERIFY(second->completed());
        QTRY_VERIFY(third->completed());
        QVERIFY(second->isError());
        QVERIFY(third->isError());
        QCOMPARE(second->executionCertainty(), QString("NotSent"));
        QCOMPARE(third->executionCertainty(), QString("NotSent"));
        QTest::qWait(30);
        QCOMPARE(server.arrivals.size(), 1);
        if (sequence)
            QVERIFY(sequence->stopped());
    }

    void cppOwnerAndEngineTeardown()
    {
        RequestServer server;
        QVERIFY(server.listen());
        server.response.clear();
        NiriSession::instance()->selectEndpoint(server.path());
        auto owner = std::make_unique<QObject>();
        QPointer<NiriPendingReply> reply =
                NiriRequests::instance()->sendWire(WireValue("Version"), {}, owner.get());
        QCOMPARE(reply->parent(), owner.get());
        QCOMPARE(QJSEngine::objectOwnership(reply), QJSEngine::CppOwnership);
        QTRY_COMPARE(server.arrivals.size(), 1);
        owner.reset();
        QVERIFY(reply.isNull());
        QTRY_COMPARE(server.peers.first()->state(), QLocalSocket::UnconnectedState);
        auto engine = std::make_unique<QQmlEngine>();
        auto *facade = NiriRequests::create(engine.get(), engine.get());
        std::unique_ptr<NiriPendingReply> pending(facade->version());
        QTRY_COMPARE(server.arrivals.size(), 2);
        engine.reset();
        QTRY_VERIFY(pending->completed());
        QCOMPARE(pending->error().category, QString("Cancelled"));
        QTRY_COMPARE(server.peers.last()->state(), QLocalSocket::UnconnectedState);
    }

    void noDeadline()
    {
        RequestServer server;
        QVERIFY(server.listen());
        server.response.clear();
        NiriSession::instance()->selectEndpoint(server.path());
        NiriRequests::instance()->setRequestTimeoutMs(20);
        std::unique_ptr<NiriPendingReply> reply(
                NiriRequests::instance()->sendRaw(QVariant("PickWindow"), {{"timeoutMs", -1}}));
        QTRY_COMPARE(server.arrivals.size(), 1);
        QTest::qWait(5100);
        QVERIFY(!reply->completed());
        server.peers.first()->write("{\"Ok\":{\"PickedWindow\":null}}\n");
        QTRY_VERIFY(reply->completed());
        QVERIFY(!reply->isError());
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (app.arguments().contains("--capture-scope")) {
        auto engine = std::make_unique<QQmlEngine>();
        auto *facade = NiriRequests::create(engine.get(), engine.get());
        QObject observer;
        QObject::connect(NiriSession::instance(), &NiriSession::socketPathChanged, &observer,
                         [&] { engine.reset(); });
        std::unique_ptr<NiriRequestSequence> sequence;
        std::unique_ptr<NiriPendingReply> reply;
        if (app.arguments().last() == "sequence") {
            sequence.reset(facade->sequence());
            if (!sequence || !sequence->stopped())
                return 2;
            reply.reset(sequence->request("Version"));
        } else {
            reply.reset(facade->version());
        }
        QTest::qWait(30);
        return !engine && reply && reply->completed() && reply->error().category == "Cancelled" ? 0
                                                                                                : 3;
    }
    TestRequests test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_requests.moc"
