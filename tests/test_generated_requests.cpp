#include "nirirequests.h"
#include "nirievents.h"
#include "generated/niriipccodec_generated.h"
#include "ipcsurfacefixtures.h"
#include <QGuiApplication>
#include <QLocalServer>
#include <QMetaMethod>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <limits>

using namespace NiriWire;
using namespace NiriSurfaceTest;

namespace {
class SurfaceServer
{
public:
    SurfaceServer()
    {
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            while (auto *peer = server.nextPendingConnection()) {
                ++connections;
                peers.append(peer);
                auto framer = std::make_shared<NiriLineBuffer>();
                QObject::connect(peer, &QLocalSocket::readyRead, &server, [this, peer, framer] {
                    framer->feed(peer->readAll(), [this, peer](const QByteArray &bytes) {
                        arrivals.append(bytes);
                        QTimer::singleShot(delay, peer, [this, peer] { peer->write(response); });
                    });
                });
            }
        });
    }
    bool listen() { return directory.isValid() && server.listen(directory.filePath("socket")); }
    QTemporaryDir directory;
    QLocalServer server;
    QList<QPointer<QLocalSocket>> peers;
    QList<QByteArray> arrivals;
    QByteArray response = "{\"Ok\":\"Handled\"}\n";
    int delay = 0;
    int connections = 0;
};

QString tagOf(const WireValue &value)
{
    return value.stringValue() ? *value.stringValue() : value.objectValue()->first().first;
}
} // namespace

class TestGeneratedRequests : public QObject
{
    Q_OBJECT
    WireValue fixtures, manifest;
    void checkActionCopies(const NiriAction &original)
    {
        NiriAction copied(original);
        QVERIFY(copied == original);
        NiriAction assigned = NiriAction::fromQuit(NiriActionQuitPayload{});
        assigned = original;
        QVERIFY(assigned == original);
        auto &self = assigned;
        assigned = self;
        QVERIFY(assigned == original);
        NiriAction moved(std::move(copied));
        QVERIFY(moved == original);
        assigned = std::move(moved);
        QVERIFY(assigned == original);
    }
    const WireValue &fixture(const QString &name) const
    {
        for (const auto &row : *fixtures.lookup("surfaceVectors")->arrayValue()) {
            if (*row.lookup("name")->stringValue() == name)
                return *row.lookup("value");
        }
        qFatal("Missing surface fixture");
    }
    const WireValue &binding(const QString &tag) const
    {
        for (const auto &row : *manifest.arrayValue()) {
            if (*row.lookup("variant")->stringValue() == tag)
                return row;
        }
        qFatal("Missing request binding");
    }
    WireValue response(const QString &tag) const
    {
        for (const auto &row : *fixtures.lookup("surfaceVectors")->arrayValue()) {
            if (*row.lookup("type")->stringValue() == "Response"
                && tagOf(*row.lookup("value")) == tag)
                return *row.lookup("value");
        }
        qFatal("Missing response fixture");
    }
    static std::unique_ptr<QObject> consumer(QQmlEngine &engine)
    {
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQml
import Niri 1.0
QtObject {
    id: root
    property NiriPendingReply retained: null
    property bool expectNull: false
    property bool observed: false
    function validate(r) {
        let x = r.result;
        switch (r.responseKind) {
        case "Handled": return x.wireName === "Handled" && r.value === "Handled";
        case "Version": return typeof x === "string" && x.indexOf("26.04") === 0;
        case "Outputs": return x["DP-1"].name === "DP-1" && x["DP-1"].currentMode === null;
        case "Workspaces": return x.length === 1 && x[0].id === "9007199254740993";
        case "Windows": return x.length === 1 && x[0].id === "18446744073709551615" && x[0].title === null;
        case "Layers": return x.length === 1 && x[0].namespaceName === "panel";
        case "KeyboardLayouts": return x.names.length === 2 && x.currentIdx === 1;
        case "FocusedOutput": return root.expectNull ? x === null : x !== null && x.name === "DP-1";
        case "FocusedWindow":
        case "PickedWindow": return root.expectNull ? x === null : x !== null && x.id === "18446744073709551615";
        case "PickedColor": return root.expectNull ? x === null : x !== null && x.rgb[1] === 0.5;
        case "OutputConfigChanged": return x.wireName === "Applied" || x.wireName === "OutputWasMissing";
        case "OverviewState": return x.isOpen === true;
        case "Casts": return x.length === 1 && x[0].streamId === "18446744073709551615";
        }
        return false;
    }
    function watch() {
        let reply = retained;
        reply.finished.connect(function() {
            root.observed = !reply.isError && root.validate(reply);
        });
    }
    function submit(name, args) {
        retained = NiriRequests[name].apply(NiriRequests, args);
        watch();
    }
    function factories(output) {
        if (output) {
            retained = NiriRequests.output("DP-1", NiriArgs.outputActionVrr({vrr: NiriArgs.vrrToSet({vrr: true, onDemand: false})}));
        } else {
            retained = NiriRequests.action(NiriArgs.actionMoveWindowToWorkspace({window_id: "123", reference: NiriArgs.workspaceReferenceArgId("456"), focus: false}));
        }
        watch();
    }
})QML",
                          QUrl());
        auto root = std::unique_ptr<QObject>(component.create());
        if (!root)
            qWarning() << component.errors();
        return root;
    }

private slots:
    void initTestCase()
    {
        fixtures = loadFixture(QFINDTESTDATA("fixtures/serde-v26.04/vectors.json"));
        fixtures = withNewerSurface(
                fixtures, loadFixture(QFINDTESTDATA("fixtures/serde-main-689c1e9/vectors.json")));
        manifest = loadFixture(QFINDTESTDATA("../generated/ipc-requests.json"));
    }
    void init()
    {
        auto *session = NiriSession::instance();
        session->stop();
        session->selectEndpoint(QString());
        session->setAutoReconnect(false);
        NiriRequests::instance()->setRequestTimeoutMs(5000);
    }
    void cleanup() { NiriSession::instance()->stop(); }
    void inventory()
    {
        QCOMPARE(manifest.arrayValue()->size(), 16);
        QSet<QByteArray> expected, actual;
        for (const auto &row : *manifest.arrayValue())
            expected.insert(row.lookup("method")->stringValue()->toLatin1());
        const auto &meta = NiriIpcRequests::staticMetaObject;
        for (int i = meta.methodOffset(); i < meta.methodCount(); ++i)
            actual.insert(meta.method(i).name());
        QCOMPARE(actual, expected);
        QCOMPARE(NiriOutputAction::staticMetaObject.enumerator(0).keyCount() - 2, 10);
        QCOMPARE(NiriAction::staticMetaObject.enumerator(0).keyCount() - 2, 149);
    }
    void everyRequest_data()
    {
        QTest::addColumn<QString>("name");
        for (const auto &row : *fixtures.lookup("surfaceVectors")->arrayValue()) {
            if (*row.lookup("type")->stringValue() == "Request") {
                const auto name = *row.lookup("name")->stringValue();
                QTest::newRow(qPrintable(name)) << name;
            }
        }
    }
    void everyRequest()
    {
        QFETCH(QString, name);
        const auto &request = fixture(name);
        const auto &entry = binding(tagOf(request));
        const auto expected = *entry.lookup("response")->stringValue();
        QVariantList args;
        for (const auto &arg : *entry.lookup("arguments")->arrayValue()) {
            const auto &payload = *request.lookup(tagOf(request));
            const auto *wire = arg.lookup("wire");
            args.append(
                    jsInput(wire->stringValue() ? *payload.lookup(*wire->stringValue()) : payload));
        }
        SurfaceServer server;
        QVERIFY(server.listen());
        server.response = expected == "Err"
                ? QByteArray("{\"Err\":\"deliberate error\"}\n")
                : *NiriWire::encode(*WireValue::object({{"Ok", response(expected)}})) + '\n';
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        QQmlEngine engine;
        auto root = consumer(engine);
        QVERIFY(root);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "submit",
                                          Q_ARG(QVariant, *entry.lookup("method")->stringValue()),
                                          Q_ARG(QVariant, args)));
        auto *reply = root->property("retained").value<NiriPendingReply *>();
        QVERIFY(reply);
        QVERIFY(!reply->completed());
        QTRY_VERIFY(reply->completed());
        QCOMPARE(reply->isError(), expected == "Err");
        if (expected == "Err")
            QCOMPARE(reply->error().category, "Server");
        else {
            QVERIFY2(root->property("observed").toBool(), qPrintable(reply->error().message));
            QCOMPARE(QString::fromLatin1(reply->metaObject()->className()),
                     "Niri" + expected + "Reply");
        }
        QCOMPARE(server.arrivals.size(), 1);
        QCOMPARE(server.connections, 1);
        const auto received = parse(server.arrivals.first());
        QVERIFY(received);
        QCOMPARE(NiriWire::encode(canonical(received->root)), NiriWire::encode(canonical(request)));
        if (tagOf(request) == "EventStream") {
            QVERIFY(NiriSession::instance()->subscribed());
            QSignalSpy event(NiriEvents::instance(), &NiriEvents::windowClosed);
            server.peers.first()->write(*NiriWire::encode(fixture("event-WindowsChanged")) + '\n');
            server.peers.first()->write("{\"WindowClosed\":{\"id\":18446744073709551615}}\n");
            QTRY_COMPARE(event.size(), 1);
            QCOMPARE(event.first().first().toString(), "18446744073709551615");
            std::unique_ptr<NiriHandledReply> again(NiriRequests::instance()->eventStream());
            QVERIFY(!again->completed());
            QTRY_VERIFY(again->completed());
            QVERIFY(!again->isError());
            QCOMPARE(again->wireDocument().bytes, reply->wireDocument().bytes);
            QCOMPARE(server.connections, 1);
            QCOMPARE(server.peers.first()->state(), QLocalSocket::ConnectedState);
        }
    }
    void typedResponses_data()
    {
        QTest::addColumn<QString>("name");
        for (const auto &row : *fixtures.lookup("surfaceVectors")->arrayValue()) {
            if (*row.lookup("type")->stringValue() == "Response") {
                const auto name = *row.lookup("name")->stringValue();
                QTest::newRow(qPrintable(name)) << name;
            }
        }
    }
    void typedResponses()
    {
        QFETCH(QString, name);
        const auto &ok = fixture(name);
        const auto expected = tagOf(ok);
        SurfaceServer server;
        QVERIFY(server.listen());
        server.response = *NiriWire::encode(*WireValue::object({{"Ok", ok}})) + '\n';
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        QQmlEngine engine;
        auto root = consumer(engine);
        QVERIFY(root);
        root->setProperty("expectNull",
                          ok.lookup(expected)
                                  && ok.lookup(expected)->type() == WireValue::Type::Null);
        auto *reply = NiriRequests::dispatchTyped(WireValue("Version"), NiriRequests::instance(),
                                                  {}, expected);
        root->setProperty("retained", QVariant::fromValue(reply));
        QVERIFY(QMetaObject::invokeMethod(root.get(), "watch"));
        QVERIFY(!reply->completed());
        QTRY_VERIFY(reply->completed());
        QVERIFY2(!reply->isError(), qPrintable(reply->error().message));
        QVERIFY(root->property("observed").toBool());
        QCOMPARE(QString::fromLatin1(reply->metaObject()->className()),
                 "Niri" + expected + "Reply");
        if (expected == "Windows") {
            const auto *typed = qobject_cast<NiriWindowsReply *>(reply);
            QVERIFY(typed);
            QCOMPARE(typed->result().first().id, std::numeric_limits<quint64>::max());
        }
    }
    void badResponses_data()
    {
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("malformed");
        QSet<QString> seen;
        for (const auto &row : *manifest.arrayValue()) {
            const auto expected = *row.lookup("response")->stringValue();
            if (seen.contains(expected))
                continue;
            seen.insert(expected);
            QTest::newRow(qPrintable(expected + "-wrong")) << expected << false;
            QTest::newRow(qPrintable(expected + "-malformed")) << expected << true;
        }
    }
    void badResponses()
    {
        QFETCH(QString, expected);
        QFETCH(bool, malformed);
        SurfaceServer server;
        QVERIFY(server.listen());
        const auto ok = malformed       ? *WireValue::object({{expected, WireValue(false)}})
                : expected == "Handled" ? response("Version")
                                        : WireValue("Handled");
        server.response = *NiriWire::encode(*WireValue::object({{"Ok", ok}})) + '\n';
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        std::unique_ptr<NiriPendingReply> reply(NiriRequests::dispatchTyped(
                WireValue("Version"), NiriRequests::instance(), {}, expected));
        QTRY_VERIFY(reply->completed());
        QVERIFY(reply->isError());
        QCOMPARE(reply->error().category, "Decode");
        QVERIFY(!reply->error().fieldPath.isEmpty());
        QCOMPARE(reply->wireDocument().bytes, server.response.trimmed());
    }
    void argumentValues_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("type");
        for (const auto &row : *fixtures.lookup("surfaceVectors")->arrayValue()) {
            const auto type = *row.lookup("type")->stringValue();
            if (type == "Action" || type == "OutputAction") {
                const auto name = *row.lookup("name")->stringValue();
                QTest::newRow(qPrintable(name)) << name << type;
            }
        }
    }
    void argumentValues()
    {
        QFETCH(QString, name);
        QFETCH(QString, type);
        const auto &wire = fixture(name);
        QVariant typed;
        NiriIpc::DecodeError error;
        QVERIFY2(NiriIpc::decodeNamed(type, wire, typed, error, NiriIpc::DecodeMode::Outbound),
                 qPrintable(error.path + error.message));
        const auto encoded = NiriIpc::encodeVariant(typed, &error);
        QVERIFY(encoded);
        QCOMPARE(NiriWire::encode(canonical(*encoded)), NiriWire::encode(canonical(wire)));
        if (type == "Action")
            checkActionCopies(typed.value<NiriAction>());
    }
    void actionCopyStates()
    {
        NiriAction invalid;
        invalid.invalidReason = "retained diagnostic";
        checkActionCopies(invalid);
        NiriAction future;
        NiriIpc::DecodeError error;
        const auto document = parse("{\"FutureAction\":{\"id\":18446744073709551615}}");
        QVERIFY(document);
        QVERIFY(NiriIpc::decode(document->root, future, &error));
        QCOMPARE(future.kind(), NiriAction::Unknown);
        checkActionCopies(future);
    }
    void picksNoDeadline_data()
    {
        QTest::addColumn<bool>("color");
        QTest::newRow("window") << false;
        QTest::newRow("color") << true;
    }
    void factories_data()
    {
        QTest::addColumn<bool>("output");
        QTest::newRow("action-value") << false;
        QTest::newRow("output-value") << true;
    }
    void factories()
    {
        QFETCH(bool, output);
        SurfaceServer server;
        QVERIFY(server.listen());
        if (output)
            server.response = "{\"Ok\":{\"OutputConfigChanged\":\"Applied\"}}\n";
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        QQmlEngine engine;
        auto root = consumer(engine);
        QVERIFY(root);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "factories", Q_ARG(QVariant, output)));
        auto *reply = root->property("retained").value<NiriPendingReply *>();
        QVERIFY(reply);
        QTRY_VERIFY(reply->completed());
        QVERIFY2(!reply->isError(), qPrintable(reply->error().message));
        QVERIFY(root->property("observed").toBool());
        QCOMPARE(server.arrivals.size(), 1);
        const auto received = parse(server.arrivals.first());
        QVERIFY(received);
        const auto expected =
                parse(output ? "{\"Output\":{\"output\":\"DP-1\",\"action\":{\"Vrr\":{\"vrr\":{"
                               "\"vrr\":true,\"on_demand\":false}}}}}"
                             : "{\"Action\":{\"MoveWindowToWorkspace\":{\"window_id\":123,"
                               "\"reference\":{\"Id\":456},\"focus\":false}}}");
        QVERIFY(expected);
        QCOMPARE(NiriWire::encode(canonical(received->root)),
                 NiriWire::encode(canonical(expected->root)));
    }
    void picksNoDeadline()
    {
        QFETCH(bool, color);
        SurfaceServer server;
        QVERIFY(server.listen());
        server.delay = 100;
        server.response =
                color ? "{\"Ok\":{\"PickedColor\":null}}\n" : "{\"Ok\":{\"PickedWindow\":null}}\n";
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        NiriRequests::instance()->setRequestTimeoutMs(10);
        std::unique_ptr<NiriPendingReply> reply(
                color ? static_cast<NiriPendingReply *>(NiriRequests::instance()->pickColor())
                      : NiriRequests::instance()->pickWindow());
        QTRY_VERIFY(reply->completed());
        QVERIFY2(!reply->isError(), qPrintable(reply->error().message));
        std::unique_ptr<NiriPendingReply> timed(
                color ? static_cast<NiriPendingReply *>(
                                NiriRequests::instance()->pickColor({{"timeoutMs", 10}}))
                      : NiriRequests::instance()->pickWindow({{"timeoutMs", 10}}));
        QTRY_VERIFY(timed->completed());
        QCOMPARE(timed->error().category, "Timeout");
    }
    void streamObservation_data()
    {
        QTest::addColumn<QString>("mode");
        for (const auto *mode : {"cancel", "stop", "reject", "bad-ack", "timeout"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void streamObservation()
    {
        QFETCH(QString, mode);
        SurfaceServer server;
        QVERIFY(server.listen());
        server.delay = mode == "reject" || mode == "bad-ack" ? 0 : 100;
        if (mode == "reject")
            server.response = "{\"Err\":\"subscription denied\"}\n";
        if (mode == "bad-ack")
            server.response = "{\"Ok\":{\"Handled\":{}}}\n";
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        std::unique_ptr<NiriHandledReply> reply(NiriRequests::instance()->eventStream(
                mode == "timeout" ? QVariantMap{{"timeoutMs", 10}} : QVariantMap{}));
        QVERIFY(!reply->completed());
        if (mode == "cancel")
            reply->cancel();
        else if (mode == "stop") {
            QTRY_COMPARE(server.arrivals.size(), 1);
            NiriSession::instance()->stop();
        }
        QTRY_VERIFY(reply->completed());
        QVERIFY(reply->isError());
        QCOMPARE(reply->error().category,
                 mode == "reject"            ? "Server"
                         : mode == "bad-ack" ? "Protocol"
                         : mode == "timeout" ? "Timeout"
                                             : "Cancelled");
        if (mode == "cancel")
            QCOMPARE(server.connections, 0);
        if (mode == "reject" || mode == "bad-ack")
            QCOMPARE(reply->wireDocument().bytes, server.response.trimmed());
        if (mode == "timeout") {
            // A caller deadline/cancellation cannot cancel the shared stream.
            QTRY_VERIFY(NiriSession::instance()->subscribed());
            QCOMPARE(server.connections, 1);
        }
    }
    void typedQmlDeclaration()
    {
        SurfaceServer server;
        QVERIFY(server.listen());
        server.response =
                *NiriWire::encode(*WireValue::object({{"Ok", response("Windows")}})) + '\n';
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        QQmlEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        component.setData(
                "import QtQml\nimport Niri 1.0\nQtObject { id: root; "
                "property NiriWindowsReply retained: null; property bool observed: false; "
                "Component.onCompleted: { let r = NiriRequests.windows(); retained = r; "
                "r.finished.connect(function() { root.observed = !r.isError && "
                "r.result[0].id === '18446744073709551615'; }); } }",
                QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QTRY_VERIFY(root->property("observed").toBool());
    }
    void invalidRequests_data()
    {
        QTest::addColumn<QString>("method");
        QTest::addColumn<QVariantList>("args");
        QTest::newRow("wrong-target-key")
                << QString("action")
                << QVariantList{
                           QVariantMap{{"MoveWindowToMonitor",
                                        QVariantMap{{"window_id", "123"}, {"output", "DP-1"}}}}};
        QTest::newRow("known-null-body")
                << QString("action")
                << QVariantList{QVariantMap{{"CloseWindow", QVariant::fromValue(nullptr)}}};
        QTest::newRow("known-array-body")
                << QString("action")
                << QVariantList{QVariantMap{{"PowerOffMonitors", QVariantList{}}}};
        QTest::newRow("future-action")
                << QString("action") << QVariantList{QVariantMap{{"Future", QVariantMap{}}}};
        QTest::newRow("future-output-action")
                << QString("output") << QVariantList{"DP-1", "Future"};
        QTest::newRow("missing-vrr-bool")
                << QString("output")
                << QVariantList{
                           "DP-1",
                           QVariantMap{{"Vrr", QVariantMap{{"vrr", QVariantMap{{"vrr", true}}}}}}};
        QTest::newRow("unknown-vrr-field")
                << QString("output")
                << QVariantList{"DP-1",
                                QVariantMap{{"Vrr",
                                             QVariantMap{{"vrr",
                                                          QVariantMap{{"vrr", true},
                                                                      {"onDemand", false},
                                                                      {"typo", false}}}}}}};
    }
    void invalidRequests()
    {
        QFETCH(QString, method);
        QFETCH(QVariantList, args);
        SurfaceServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        QQmlEngine engine;
        auto root = consumer(engine);
        QVERIFY(root);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "submit", Q_ARG(QVariant, method),
                                          Q_ARG(QVariant, args)));
        auto *reply = root->property("retained").value<NiriPendingReply *>();
        QVERIFY(reply);
        QVERIFY(!reply->completed());
        QTRY_VERIFY(reply->completed());
        QVERIFY(reply->isError());
        QCOMPARE(reply->error().category, "Configuration");
        QCOMPARE(reply->executionCertainty(), "NotSent");
        QCOMPARE(server.connections, 0);
    }
};

QTEST_MAIN(TestGeneratedRequests)
#include "test_generated_requests.moc"
