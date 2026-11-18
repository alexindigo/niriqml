#include "nirievents.h"
#include "nirieventlog.h"
#include "nirisession.h"
#include "ipcsurfacefixtures.h"
#include "streamfixture.h"
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaMethod>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include "socketdirectory.h"
#include <QTest>
#include <limits>

using namespace NiriWire;
using namespace NiriSurfaceTest;

namespace {
class EventServer
{
public:
    EventServer()
    {
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            while (auto *peer = server.nextPendingConnection()) {
                auto framer = std::make_shared<NiriLineBuffer>();
                QObject::connect(peer, &QLocalSocket::readyRead, &server, [this, peer, framer] {
                    framer->feed(peer->readAll(), [this, peer](const QByteArray &line) {
                        arrivals.append(line);
                        peer->write("{\"Ok\":\"Handled\"}\n" + event + '\n');
                    });
                });
            }
        });
    }
    bool listen() { return directory.isValid() && server.listen(directory.filePath("socket")); }
    SocketDirectory directory;
    QLocalServer server;
    QList<QByteArray> arrivals;
    QByteArray event;
};
QMetaMethod signal(const QMetaObject *meta, const QByteArray &name)
{
    for (int i = 0; i < meta->methodCount(); ++i) {
        const auto method = meta->method(i);
        if (method.methodType() == QMetaMethod::Signal && method.name() == name)
            return method;
    }
    return {};
}
} // namespace

class TestGeneratedEvents : public QObject
{
    Q_OBJECT
    WireValue fixtures, manifest;
    static constexpr quint64 Max = std::numeric_limits<quint64>::max();
    const WireValue &fixture(const QString &name) const
    {
        for (const auto &row : *fixtures.lookup("surfaceVectors")->arrayValue()) {
            if (*row.lookup("name")->stringValue() == name)
                return *row.lookup("value");
        }
        qFatal("Missing event fixture");
    }
    const WireValue &binding(const QString &tag) const
    {
        for (const auto &row : *manifest.arrayValue()) {
            if (*row.lookup("variant")->stringValue() == tag)
                return row;
        }
        qFatal("Missing event binding");
    }
    std::unique_ptr<QObject> consumer(QQmlEngine &engine, const WireValue &entry,
                                      const WireValue &payload)
    {
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        const auto method = *entry.lookup("signal")->stringValue();
        QStringList args;
        for (const auto &arg : *entry.lookup("arguments")->arrayValue())
            args.append(*arg.lookup("name")->stringValue());
        const auto handler = "on" + method.left(1).toUpper() + method.mid(1);
        QByteArray source = R"QML(
import QtQml
import Niri 1.0
QtObject {
    id: root
    property int count: 0
    property int rawCount: 0
    property bool observed: false
    property bool expectNull: false
    property bool expectedOn: false
    property string expectedPath: ""
    function accept(name, a) {
        root.count++;
        switch (name) {
        case "workspacesChanged": root.observed = a[0].length === 1 && a[0][0].id === "9007199254740993"; break;
        case "workspaceUrgencyChanged": root.observed = a[0] === "18446744073709551615" && a[1] === true; break;
        case "workspaceActivated": root.observed = a[0] === "18446744073709551615" && a[1] === false; break;
        case "workspaceActiveWindowChanged": root.observed = a[0] === "18446744073709551615" && (root.expectNull ? a[1] === null : a[1] === "0"); break;
        case "windowsChanged": root.observed = a[0].length === 1 && a[0][0].id === "18446744073709551615" && a[0][0].title === null; break;
        case "windowOpenedOrChanged": root.observed = a[0].id === "18446744073709551615"; break;
        case "windowClosed": root.observed = a[0] === "18446744073709551615"; break;
        case "windowFocusChanged": root.observed = root.expectNull ? a[0] === null : a[0] === "18446744073709551615"; break;
        case "windowFocusTimestampChanged": root.observed = a[0] === "18446744073709551615" && (root.expectNull ? a[1] === null : a[1].secs === "18446744073709551615"); break;
        case "windowUrgencyChanged": root.observed = a[0] === "18446744073709551615" && a[1] === false; break;
        case "windowLayoutsChanged": root.observed = a[0].length === 1 && a[0][0].id === "18446744073709551615" && a[0][0].layout.tileSize[0] === 1.25; break;
        case "keyboardLayoutsChanged": root.observed = a[0].names.length === 2 && a[0].currentIdx === 1; break;
        case "keyboardLayoutSwitched": root.observed = a[0] === 255; break;
        case "overviewOpenedOrClosed": root.observed = a[0] === true; break;
        case "configLoaded": root.observed = a[0] === false; break;
        case "screenshotCaptured": root.observed = root.expectNull ? a[0] === null : a[0] === root.expectedPath; break;
        case "castsChanged": root.observed = a[0].length === 1 && a[0][0].streamId === "18446744073709551615"; break;
        case "castStartedOrChanged": root.observed = a[0].streamId === "18446744073709551615"; break;
        case "castStopped": root.observed = a[0] === "18446744073709551615"; break;
        case "touchpadToggled": root.observed = a[0] === root.expectedOn; break;
        }
    }
    property Connections listener: Connections {
        target: NiriEvents
        function onRawEvent(name, payload) { root.rawCount++; }
)QML";
        source += "function " + handler.toUtf8() + "(" + args.join(", ").toUtf8()
                + ") { root.accept('" + method.toUtf8() + "', [" + args.join(", ").toUtf8()
                + "]); }\n} }";
        component.setData(source, QUrl());
        auto root = std::unique_ptr<QObject>(component.create());
        if (!root)
            qWarning() << component.errors();
        if (root) {
            for (const auto &member : *payload.objectValue()) {
                if (member.second.type() == WireValue::Type::Null)
                    root->setProperty("expectNull", true);
            }
            if (const auto *path = payload.lookup("path"); path && path->stringValue())
                root->setProperty("expectedPath", *path->stringValue());
            if (const auto *on = payload.lookup("on"); on && on->boolValue())
                root->setProperty("expectedOn", *on->boolValue());
        }
        return root;
    }
    void checkNative(const QString &tag, const QList<QVariant> &args, const WireValue &payload)
    {
        if (tag == "WorkspacesChanged") {
            QCOMPARE(args[0].value<QList<NiriWorkspace>>().first().id,
                     quint64(9007199254740993ULL));
        } else if (tag == "WindowsChanged") {
            QCOMPARE(args[0].value<QList<NiriWindow>>().first().id, Max);
        } else if (tag == "WindowOpenedOrChanged") {
            QCOMPARE(args[0].value<NiriWindow>().id, Max);
        } else if (tag == "CastsChanged") {
            QCOMPARE(args[0].value<QList<NiriCast>>().first().streamId, Max);
        } else if (tag == "CastStartedOrChanged") {
            QCOMPARE(args[0].value<NiriCast>().streamId, Max);
        } else if (tag == "WindowLayoutsChanged") {
            const auto entries = args[0].value<QList<NiriWindowLayoutChange>>();
            QCOMPARE(entries.size(), 1);
            QCOMPARE(entries.first().id, Max);
            QCOMPARE(entries.first().layout.tileSize[0], 1.25);
        } else if (tag == "KeyboardLayoutsChanged") {
            QCOMPARE(args[0].value<NiriKeyboardLayouts>().currentIdx, quint8(1));
        } else if (tag == "KeyboardLayoutSwitched") {
            QCOMPARE(args[0].metaType(), QMetaType::fromType<quint8>());
            QCOMPARE(args[0].value<quint8>(), quint8(255));
        } else if (tag == "ScreenshotCaptured") {
            const auto expected = payload.lookup("path")->stringValue();
            const auto actual = args[0].value<std::optional<QString>>();
            QCOMPARE(actual, expected ? std::optional<QString>(*expected) : std::nullopt);
        } else if (tag == "WindowFocusChanged") {
            const auto actual = args[0].value<std::optional<quint64>>();
            QCOMPARE(actual,
                     payload.lookup("id")->numberValue() ? std::optional<quint64>(Max)
                                                         : std::nullopt);
        } else if (tag == "TouchpadToggled") {
            QCOMPARE(args[0].metaType(), QMetaType::fromType<bool>());
            QCOMPARE(args[0].toBool(), *payload.lookup("on")->boolValue());
        } else if (tag == "OverviewOpenedOrClosed" || tag == "ConfigLoaded") {
            QCOMPARE(args[0].metaType(), QMetaType::fromType<bool>());
            QCOMPARE(args[0].toBool(), tag == "OverviewOpenedOrClosed");
        } else {
            QCOMPARE(args[0].metaType(), QMetaType::fromType<quint64>());
            QCOMPARE(args[0].value<quint64>(), Max);
            if (tag == "WorkspaceActiveWindowChanged") {
                const auto actual = args[1].value<std::optional<quint64>>();
                QCOMPARE(actual,
                         payload.lookup("active_window_id")->numberValue()
                                 ? std::optional<quint64>(0)
                                 : std::nullopt);
            } else if (tag == "WindowFocusTimestampChanged") {
                const auto actual = args[1].value<std::optional<NiriTimestamp>>();
                QCOMPARE(actual.has_value(),
                         payload.lookup("focus_timestamp")->type() != WireValue::Type::Null);
                if (actual)
                    QCOMPARE(actual->secs, Max);
            } else if (args.size() == 2) {
                QCOMPARE(args[1].metaType(), QMetaType::fromType<bool>());
                QCOMPARE(args[1].toBool(), tag == "WorkspaceUrgencyChanged");
            }
        }
    }
private slots:
    void initTestCase()
    {
        fixtures = loadFixture(QFINDTESTDATA("fixtures/serde-v26.04/vectors.json"));
        fixtures = withNewerSurface(
                fixtures, loadFixture(QFINDTESTDATA("fixtures/serde-main-689c1e9/vectors.json")));
        manifest = loadFixture(QFINDTESTDATA("../generated/ipc-events.json"));
    }
    void init()
    {
        NiriSession::instance()->stop();
        NiriSession::instance()->setAutoReconnect(false);
    }
    void cleanup() { NiriSession::instance()->stop(); }
    void everyEvent_data()
    {
        QTest::addColumn<QString>("name");
        for (const auto &row : *fixtures.lookup("surfaceVectors")->arrayValue()) {
            if (*row.lookup("type")->stringValue() == "Event") {
                const auto name = *row.lookup("name")->stringValue();
                QTest::newRow(qPrintable(name)) << name;
            }
        }
    }
    void everyEvent()
    {
        QFETCH(QString, name);
        const auto &wire = fixture(name);
        const auto &member = wire.objectValue()->first();
        const auto &entry = binding(member.first);
        NiriEvent event;
        NiriIpc::DecodeError detail;
        QVERIFY2(NiriIpc::decodeEvent(wire, event, detail),
                 qPrintable(detail.path + detail.message));
        const auto encoded = NiriIpc::encode(event);
        QVERIFY(encoded);
        QCOMPARE(NiriWire::encode(canonical(*encoded)), NiriWire::encode(canonical(wire)));
        StateStreamFixture server;
        QVERIFY(server.start());
        // Typed publication tests use valid stream preconditions, not isolated
        // strict deltas that upstream's state helper would panic on.
        NiriEventStreamState initial;
        NiriWindow win;
        win.id = Max;
        NiriWorkspace ws;
        ws.id = Max;
        NiriCast cast;
        cast.streamId = Max;
        cast.kind = NiriCastKind(NiriCastKind::PipeWire);
        cast.target = NiriCastTarget::fromNothing({});
        initial.apply(NiriEvent::fromWindowsChanged({{win}, {}}), 0);
        initial.apply(NiriEvent::fromWorkspacesChanged({{ws}, {}}), 0);
        initial.apply(NiriEvent::fromKeyboardLayoutsChanged({{}, {}}), 0);
        initial.apply(NiriEvent::fromCastsChanged({{cast}, {}}), 0);
        for (const auto &part : initial.replicate())
            server.send(*NiriWire::encode(*NiriIpc::encode(part)));
        QTRY_VERIFY(NiriSession::instance()->state().initialStateReceived());
        auto *events = NiriEvents::instance();
        const auto method = entry.lookup("signal")->stringValue()->toLatin1();
        QSignalSpy native(events->nativeEvents(),
                          signal(events->nativeEvents()->metaObject(), method));
        QSignalSpy qml(events, signal(events->metaObject(), method));
        QSignalSpy raw(events, &NiriEvents::rawEvent);
        QSignalSpy errors(events, &NiriEvents::decodeError);
        QVERIFY(native.isValid());
        QVERIFY(qml.isValid());
        QQmlEngine engine;
        auto root = consumer(engine, entry, member.second);
        QVERIFY(root);
        server.send(*NiriWire::encode(wire));
        QTRY_COMPARE(root->property("count").toInt(), 1);
        QVERIFY(root->property("observed").toBool());
        QCOMPARE(root->property("rawCount").toInt(), 1);
        QCOMPARE(native.size(), 1);
        QCOMPARE(qml.size(), 1);
        QCOMPARE(raw.size(), 1);
        QCOMPARE(errors.size(), 0);
        checkNative(member.first, native.first(), member.second);
        QCOMPARE(server.request, QByteArray("\"EventStream\"\n"));
    }
    void nativeEpochStop_data() { everyEvent_data(); }
    void nativeEpochStop()
    {
        QFETCH(QString, name);
        const auto &wire = fixture(name);
        const auto &member = wire.objectValue()->first();
        const auto &entry = binding(member.first);
        auto *events = NiriEvents::instance();
        const auto method = entry.lookup("signal")->stringValue()->toLatin1();
        const auto nativeMethod = signal(events->nativeEvents()->metaObject(), method);
        QSignalSpy native(events->nativeEvents(), nativeMethod);
        QSignalSpy qml(events, signal(events->metaObject(), method));
        QSignalSpy raw(events, &NiriEvents::rawEvent);
        const auto stop = metaObject()->method(metaObject()->indexOfSlot("stopDuringEvent()"));
        const auto connection = connect(events->nativeEvents(), nativeMethod, this, stop);
        QVERIFY(connection);
        events->dispatchWireEvent(member.first, member.second, NiriSession::instance()->epoch());
        disconnect(connection);
        QCOMPARE(native.size(), 1);
        QCOMPARE(qml.size(), 0);
        QCOMPARE(raw.size(), 0);
    }
    void diagnostics_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<bool>("unknown");
        QTest::addColumn<QByteArray>("qmlCheck");
        QTest::newRow("future-object")
                << QByteArray("{\"Future\":{\"id\":18446744073709551615}}") << true
                << QByteArray("payload.id === '18446744073709551615'");
        QTest::newRow("future-scalar")
                << QByteArray("{\"Future\":42}") << true << QByteArray("payload === '42'");
        QTest::newRow("future-unit")
                << QByteArray("\"FutureUnit\"") << true << QByteArray("payload === 'FutureUnit'");
        QTest::newRow("known-unit-malformed") << QByteArray("\"WindowClosed\"") << false
                                              << QByteArray("payload === 'WindowClosed'");
        QTest::newRow("optional-body-null") << QByteArray("{\"WindowFocusChanged\":null}") << false
                                            << QByteArray("payload === null");
        QTest::newRow("optional-body-array")
                << QByteArray("{\"ScreenshotCaptured\":[]}") << false
                << QByteArray("payload !== null && payload.length === 0");
        QTest::newRow("bad-known-id") << QByteArray("{\"WindowClosed\":{\"id\":\"not-number\"}}")
                                      << false << QByteArray("payload.id === 'not-number'");
    }
    void diagnostics()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(bool, unknown);
        QFETCH(QByteArray, qmlCheck);
        EventServer server;
        QVERIFY(server.listen());
        server.event = bytes;
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        auto *events = NiriEvents::instance();
        QSignalSpy raw(events, &NiriEvents::rawEvent);
        QSignalSpy errors(events, &NiriEvents::decodeError);
        QSignalSpy future(events, &NiriEvents::unknownEvent);
        QSignalSpy closed(events->nativeEvents(), &NiriNativeEvents::windowClosed);
        QSignalSpy focus(events->nativeEvents(), &NiriNativeEvents::windowFocusChanged);
        NiriEventLog log;
        QQmlEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        component.setData(
                QByteArray("import QtQml\nimport Niri 1.0\nQtObject { id: root; "
                           "property bool observed: false; property int unknowns: 0; property int "
                           "errors: 0; "
                           "property Connections listener: Connections { target: NiriEvents; "
                           "function onUnknownEvent(name, payload) { root.unknowns++; } "
                           "function onDecodeError(path, message, payload) { root.errors++; } "
                           "function onRawEvent(name, payload) { root.observed = ")
                        + qmlCheck + "; } } }",
                QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        NiriSession::instance()->start();
        QTRY_COMPARE(raw.size(), 1);
        QCOMPARE(errors.size(), unknown ? 0 : 1);
        QCOMPARE(future.size(), unknown ? 1 : 0);
        QCOMPARE(closed.size(), 0);
        QCOMPARE(focus.size(), 0);
        QVERIFY(root->property("observed").toBool());
        QCOMPARE(root->property("unknowns").toInt(), unknown ? 1 : 0);
        QCOMPARE(root->property("errors").toInt(), unknown ? 0 : 1);
        QVERIFY(NiriSession::instance()->subscribed());
        const auto doc = parse(bytes);
        QVERIFY(doc);
        NiriEvent decoded;
        NiriIpc::DecodeError detail;
        QCOMPARE(NiriIpc::decodeEvent(doc->root, decoded, detail), unknown);
        if (unknown) {
            QVERIFY(!decoded.known());
            QVERIFY(decoded.valid());
            const auto encoded = NiriIpc::encode(decoded);
            QVERIFY(encoded);
            QCOMPARE(*encoded, doc->root);
        }
        const auto &payload =
                doc->root.stringValue() ? doc->root : doc->root.objectValue()->first().second;
        const auto expected = NiriWire::toQmlProjection(payload);
        QCOMPARE(raw.first().at(1), expected);
        QCOMPARE(log.rowCount(), 1);
        QCOMPARE(log.data(log.index(0), NiriEventLog::PayloadRole), expected);
    }
    void commitBeforeNative()
    {
        StateStreamFixture server;
        QVERIFY(server.start());
        auto *events = NiriEvents::instance();
        bool observed = false;
        const auto connection =
                connect(events->nativeEvents(), &NiriNativeEvents::windowsChanged, this,
                        [&](const QList<NiriWindow> &windows) {
                            observed = events->lastWindowsSnapshot().size() == windows.size()
                                    && events->lastWindowsSnapshot().first().value<NiriWindow>().id
                                            == windows.first().id;
                        });
        const auto &wire = fixture("event-WindowsChanged");
        server.send(*NiriWire::encode(wire));
        QTRY_VERIFY(observed);
        disconnect(connection);
        QVERIFY(observed);
    }
    void retainedEventExtensions()
    {
        const auto doc = parse(
                "{\"WindowClosed\":{\"id\":18446744073709551615,\"future\":18446744073709551614}}");
        QVERIFY(doc);
        NiriEvent event;
        NiriIpc::DecodeError detail;
        QVERIFY(NiriIpc::decodeEvent(doc->root, event, detail));
        QVERIFY(event.known());
        const auto &payload = std::get<NiriEvent::WindowClosedValue>(event.value).payload;
        QCOMPARE(payload.id, Max);
        QCOMPARE(payload.extensions.size(), 1);
        QCOMPARE(payload.extensions.first().second.numberValue()->toUInt64(),
                 std::optional<quint64>(Max - 1));
        const auto encoded = NiriIpc::encode(event);
        QVERIFY(encoded);
        QCOMPARE(NiriWire::encode(canonical(*encoded)), NiriWire::encode(canonical(doc->root)));
    }
public slots:
    void stopDuringEvent() { NiriSession::instance()->stop(); }
};

QTEST_MAIN(TestGeneratedEvents)
#include "test_generated_events.moc"
