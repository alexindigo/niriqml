#include <QFile>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTest>
#include <QMetaProperty>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QScopeGuard>
#include "generated/niriipccodec_generated.h"
#include "nirievents.h"
#include "niristate.h"
#include "niriwindowsmodel.h"
#include "niriwindowreactive.h"
#include "nirisession.h"
#include "nirirequests.h"
#include "niripendingreply.h"

using namespace NiriWire;
using namespace NiriIpc;

namespace {
bool equivalent(const WireValue &left, const WireValue &right)
{
    if (left.type() != right.type())
        return false;
    if (left.numberValue()) {
        const auto a = left.numberValue()->toUInt64();
        const auto b = right.numberValue()->toUInt64();
        if (a && b)
            return a == b;
        const auto signedA = left.numberValue()->toInt64();
        const auto signedB = right.numberValue()->toInt64();
        if (signedA && signedB)
            return signedA == signedB;
        return left.numberValue()->toDouble() == right.numberValue()->toDouble();
    }
    if (left.objectValue()) {
        if (left.objectValue()->size() != right.objectValue()->size())
            return false;
        for (const auto &member : *left.objectValue()) {
            const auto *item = right.lookup(member.first);
            if (!item || !equivalent(member.second, *item))
                return false;
        }
        return true;
    }
    if (left.arrayValue()) {
        if (left.arrayValue()->size() != right.arrayValue()->size())
            return false;
        for (qsizetype i = 0; i < left.arrayValue()->size(); ++i) {
            if (!equivalent(left.arrayValue()->at(i), right.arrayValue()->at(i)))
                return false;
        }
        return true;
    }
    return left == right;
}

WireValue parsed(const QByteArray &json)
{
    const auto result = parse(json);
    if (!result)
        qFatal("Invalid test JSON");
    return result->root;
}

WireValue replaced(const WireValue &record, const QString &key, const WireValue &value)
{
    auto members = *record.objectValue();
    for (auto &member : members) {
        if (member.first == key) {
            member.second = value;
            return *WireValue::object(members);
        }
    }
    members.append({key, value});
    return *WireValue::object(members);
}

QString camel(const QString &wire)
{
    auto parts = wire.split('_');
    QString name = parts.takeFirst();
    for (auto part : parts) {
        part[0] = part[0].toUpper();
        name += part;
    }
    return name;
}

class CodecServer : public QObject
{
public:
    QTemporaryDir directory;
    QLocalServer server;
    QLocalSocket *stream = nullptr;
    QMap<QString, WireValue> responses;
    QByteArray initialEvents;
    CodecServer()
    {
        connect(&server, &QLocalServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                    while (socket->canReadLine()) {
                        const auto request = parse(socket->readLine());
                        if (!request || !request->root.stringValue())
                            continue;
                        const auto name = *request->root.stringValue();
                        if (name == "EventStream") {
                            stream = socket;
                            socket->write("{\"Ok\":\"Handled\"}\n" + initialEvents);
                        } else if (responses.contains(name)) {
                            const auto ok = *WireValue::object({{name, responses[name]}});
                            socket->write(*NiriWire::encode(*WireValue::object({{"Ok", ok}}))
                                          + '\n');
                        }
                    }
                });
            }
        });
    }
    bool listen() { return directory.isValid() && server.listen(directory.filePath("socket")); }
};
} // namespace

class TestCodec : public QObject
{
    Q_OBJECT
    WireValue fixtures;
    WireValue fixture(const QString &name) const
    {
        for (const auto &row : *fixtures.lookup("vectors")->arrayValue()) {
            if (*row.lookup("name")->stringValue() == name)
                return *row.lookup("value");
        }
        qFatal("Missing fixture");
    }

private slots:
    void initTestCase()
    {
        QFile file(QFINDTESTDATA("fixtures/serde-v26.04/vectors.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        fixtures = parsed(file.readAll());
    }

    void serdeRoundtrip_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("type");
        for (const auto &row : *fixtures.lookup("vectors")->arrayValue()) {
            const auto name = *row.lookup("name")->stringValue();
            QTest::newRow(qPrintable(name)) << name << *row.lookup("type")->stringValue();
        }
    }
    void serdeRoundtrip()
    {
        QFETCH(QString, name);
        QFETCH(QString, type);
        const auto wire = fixture(name);
        QVariant typed;
        DecodeError error;
        QVERIFY2(decodeNamed(type, wire, typed, error), qPrintable(error.path + error.message));
        const auto encoded = encodeVariant(typed, &error);
        QVERIFY2(encoded.has_value(), qPrintable(error.path + error.message));
        QVERIFY2(equivalent(wire, *encoded),
                 qPrintable(QString::fromUtf8(*NiriWire::encode(*encoded))));
    }

    void exactIds_data()
    {
        QTest::addColumn<QString>("text");
        for (const auto &text :
             {"0", "9007199254740993", "9223372036854775808", "18446744073709551615"})
            QTest::newRow(text) << QString::fromLatin1(text);
    }
    void exactIds()
    {
        QFETCH(QString, text);
        const auto token = NumberToken::fromText(text.toLatin1());
        QVERIFY(token);
        auto wire = replaced(fixture("window-present"), "id", WireValue(*token));
        wire = replaced(wire, "workspace_id", WireValue(*token));
        wire = replaced(wire, "is_focused", WireValue(true));
        NiriWindow window;
        QVERIFY(decode(wire, window));
        QCOMPARE(window.id, *token->toUInt64());
        QCOMPARE(window.workspaceId, token->toUInt64());
        QCOMPARE(window.qmlId(), text);
        QCOMPARE(window.qmlWorkspaceId().toString(), text);
        auto *events = NiriEvents::instance();
        NiriWindowsModel model;
        NiriWindowReactive reactive;
        reactive.setWindowId(*token->toUInt64());
        auto *state = NiriState::instance();
        NiriSession::instance()->stop();
        events->dispatchWireEvent(
                "WindowsChanged",
                *WireValue::object({{"windows", WireValue(WireValue::Array{wire})}}),
                NiriSession::instance()->epoch());
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 0), NiriWindowsModel::IdRole).metaType(),
                 QMetaType::fromType<QString>());
        QCOMPARE(model.data(model.index(0, 0), NiriWindowsModel::IdRole).toString(), text);
        QVERIFY(reactive.valid());
        QCOMPARE(reactive.id(), *token->toUInt64());
        QCOMPARE(state->focusedWindowId(), token->toUInt64());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("windowValue", QVariant::fromValue(window));
        engine.rootContext()->setContextProperty("expected", text);
        engine.rootContext()->setContextProperty("reactive", &reactive);
        engine.rootContext()->setContextProperty("state", state);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            QtObject {
                property bool passed: typeof windowValue.id === "string"
                    && windowValue.id === expected && windowValue.workspaceId === expected
                    && reactive.valid && reactive.id === expected && reactive.windowId === expected
                    && state.focusedWindowId === expected && state.focusedWindowTitle === ""
            }
        )",
                          QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QVERIFY(object->property("passed").toBool());
        const auto encoded = NiriIpc::encode(window);
        QVERIFY(encoded);
        QCOMPARE(encoded->lookup("id")->numberValue()->lexeme(), text.toLatin1());
        auto cleared = replaced(wire, "title", WireValue());
        cleared = replaced(cleared, "workspace_id", WireValue());
        cleared = replaced(cleared, "pid", WireValue());
        events->dispatchWireEvent("WindowOpenedOrChanged",
                                  *WireValue::object({{"window", cleared}}),
                                  NiriSession::instance()->epoch());
        QVERIFY(!reactive.title());
        QVERIFY(!reactive.workspaceId());
        QCOMPARE(model.data(model.index(0, 0), NiriWindowsModel::TitleRole).metaType(),
                 QMetaType::fromType<std::nullptr_t>());
        QVERIFY(!state->focusedWindowTitle());
        events->dispatchWireEvent("WindowFocusChanged", *WireValue::object({{"id", WireValue()}}),
                                  NiriSession::instance()->epoch());
        QVERIFY(!state->focusedWindowId());
        QVERIFY(!reactive.isFocused());
    }

    void nullAndPresentInQml()
    {
        QVariantList rows;
        QSet<QString> seen;
        for (const auto &row : *fixtures.lookup("vectors")->arrayValue()) {
            const auto &wire = *row.lookup("value");
            if (!wire.objectValue())
                continue;
            QVariant typed;
            DecodeError error;
            const auto type = *row.lookup("type")->stringValue();
            QVERIFY(decodeNamed(type, wire, typed, error));
            QVariantMap nulls;
            for (const auto &member : *wire.objectValue()) {
                if (member.second.type() != WireValue::Type::Null)
                    continue;
                const auto propertyName = camel(member.first);
                const auto *meta = typed.metaType().metaObject();
                const auto property =
                        meta->property(meta->indexOfProperty(qPrintable(propertyName)));
                QCOMPARE(property.readOnGadget(typed.constData()).metaType(),
                         QMetaType::fromType<std::nullptr_t>());
                nulls.insert(propertyName, true);
                seen.insert(type + "." + member.first);
            }
            rows.append(QVariantMap{{"value", typed}, {"nulls", nulls}});
        }
        QCOMPARE(seen.size(), 17);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("rows", rows);
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import Niri as N
            QtObject {
                property bool passed: false
                Component.onCompleted: {
                    for (const row of rows) {
                        for (const key in row.nulls) {
                            if (row.value[key] !== null) throw new Error("not null: " + key);
                        }
                    }
                    const mode = N.NiriArgs.configuredMode({width: 0, height: 0, refresh: 0});
                    const reference = N.NiriArgs.workspaceReferenceArgId("18446744073709551615");
                    const change = N.NiriArgs.sizeChangeSetFixed(-2147483648);
                    const target = N.NiriArgs.castTargetNothing({});
                    if (mode.refresh !== 0 || reference.id !== "18446744073709551615"
                        || !reference.valid || change.setFixed !== -2147483648 || !target.known)
                        throw new Error("bad constructed value");
                    const invalid = N.NiriArgs.configuredMode({width: 0, height: 0, typo: false});
                    if (invalid.valid !== false || invalid.error.length === 0)
                        throw new Error("unknown field accepted");
                    passed = true;
                }
            }
        )",
                          QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QVERIFY(object->property("passed").toBool());
    }

    void forwardCompatibility()
    {
        QFile unknown(QFINDTESTDATA("fixtures/unknown-variants.json"));
        QVERIFY(unknown.open(QIODevice::ReadOnly));
        const auto samples = parsed(unknown.readAll());
        for (const auto &sample : *samples.lookup("vectors")->arrayValue()) {
            QVariant typed;
            DecodeError detail;
            const auto &wire = *sample.lookup("value");
            QVERIFY(decodeNamed(*sample.lookup("type")->stringValue(), wire, typed, detail));
            QCOMPARE(*encodeVariant(typed), wire);
            const auto *meta = typed.metaType().metaObject();
            QVERIFY(!meta->property(meta->indexOfProperty("known"))
                             .readOnGadget(typed.constData())
                             .toBool());
        }
        NiriTransform transform;
        QVERIFY(decode(parsed("\"FutureTransform\""), transform));
        QCOMPARE(transform.kind(), NiriTransform::Unknown);
        QCOMPARE(transform.wireName(), "FutureTransform");
        QCOMPARE(*NiriIpc::encode(transform), parsed("\"FutureTransform\""));
        for (const auto &json :
             {"\"FutureUnit\"", "{\"FutureData\":{\"id\":18446744073709551615}}"}) {
            NiriSizeChange value;
            const auto wire = parsed(json);
            QVERIFY(decode(wire, value));
            QCOMPARE(value.kind(), NiriSizeChange::Unknown);
            QCOMPARE(*NiriIpc::encode(value), wire);
            DecodeError error;
            QVERIFY(!decode(wire, value, &error, "$", DecodeMode::Outbound));
        }
        NiriOutput original, extended;
        QVERIFY(decode(fixture("output-null"), original));
        auto wire =
                replaced(fixture("output-null"), "future_field", parsed("18446744073709551615"));
        QVERIFY(decode(wire, extended));
        QVERIFY(original != extended);
        QCOMPARE(extended.extensions.size(), 1);
        QVERIFY(equivalent(*NiriIpc::encode(extended), wire));
        DecodeError error;
        QVERIFY(!decode(wire, extended, &error, "$", DecodeMode::Outbound));
        QCOMPARE(error.path, "$['future_field']");
        for (const auto &key : {"make", "model", "serial"}) {
            QVERIFY(decode(replaced(fixture("output-null"), key, WireValue("metadata")), extended));
            QVERIFY(original != extended);
        }
        extended.extensions = {{"duplicate", WireValue()}, {"duplicate", WireValue()}};
        QVERIFY(!NiriIpc::encode(extended, &error));
        QCOMPARE(extended.qmlExtensions().metaType(), QMetaType::fromType<std::nullptr_t>());
    }

    void namedTupleAndEnums()
    {
        const auto layout = fixture("window-layout-present");
        const auto wire = WireValue(WireValue::Array{parsed("18446744073709551615"), layout});
        NiriWindowLayoutChange change;
        QVERIFY(decode(wire, change));
        QCOMPARE(change.id, std::numeric_limits<quint64>::max());
        QVERIFY(equivalent(*NiriIpc::encode(change), wire));
        QSignalSpy changes(NiriEvents::instance(), &NiriEvents::windowLayoutsChanged);
        NiriEvents::instance()->dispatchWireEvent(
                "WindowLayoutsChanged",
                *WireValue::object({{"changes", WireValue(WireValue::Array{wire})}}),
                NiriSession::instance()->epoch());
        QCOMPARE(changes.count(), 1);
        QCOMPARE(changes.first().first().toList().first().metaType(),
                 QMetaType::fromType<NiriWindowLayoutChange>());
        QQmlEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        engine.rootContext()->setContextProperty("entry", QVariant::fromValue(change));
        engine.rootContext()->setContextProperty(
                "future", QVariant::fromValue(NiriTransform::fromWireName("Future")));
        QVariant output, timestamp, cast;
        DecodeError detail;
        QVERIFY(decodeNamed("Output", fixture("output-present"), output, detail));
        QVERIFY(decodeNamed("Timestamp", fixture("timestamp"), timestamp, detail));
        QVERIFY(decodeNamed("Cast", fixture("cast-present"), cast, detail));
        engine.rootContext()->setContextProperty("outputValue", output);
        engine.rootContext()->setContextProperty("timestampValue", timestamp);
        engine.rootContext()->setContextProperty("castValue", cast);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import Niri as N
            QtObject {
                property bool passed: entry.id === "18446744073709551615"
                    && entry.layout.posInScrollingLayout[0] === "9007199254740993"
                    && N.NiriArgs.transformRotate90().kind === N.NiriTransform.Rotate90
                    && N.NiriArgs.transformRotate180().wireName === "180"
                    && N.NiriArgs.layerOverlay().kind === N.NiriLayer.Overlay
                    && future.kind === N.NiriTransform.Unknown && !future.known
                    && outputValue.currentMode === "18446744073709551615"
                    && outputValue.physicalSize[0] === 4294967295
                    && outputValue.logical.width === 4294967295
                    && timestampValue.secs === "18446744073709551615"
                    && timestampValue.nanos === 4294967295
                    && castValue.streamId === "18446744073709551615"
                    && castValue.sessionId === "9007199254740993"
                    && castValue.target.window.id === "18446744073709551615"
                    && castValue.pid === -2147483648 && castValue.pwNodeId === 4294967295
            }
        )",
                          QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QVERIFY(object->property("passed").toBool());
        DecodeError error;
        QVERIFY(!decode(parsed("[1,{}]"), change, &error));
        QCOMPARE(error.path, "$[1]['tile_size']");
        QVERIFY(!decode(parsed("[1]"), change, &error));
        QCOMPARE(error.path, "$");
    }

    void optionalZeroAndOmission()
    {
        struct Record
        {
            const char *type;
            const char *fixture;
            WireValue::Object present;
        };
        const auto zero = parsed("0");
        const auto pair = parsed("[0,0]");
        const auto timestamp = parsed("{\"secs\":0,\"nanos\":0}");
        const auto logical = parsed(
                "{\"x\":0,\"y\":0,\"width\":0,\"height\":0,\"scale\":0,\"transform\":\"Normal\"}");
        const QList<Record> records = {
                {"Window",
                 "window-null",
                 {{"title", WireValue("")},
                  {"app_id", WireValue("")},
                  {"pid", zero},
                  {"workspace_id", zero},
                  {"focus_timestamp", timestamp}}},
                {"Workspace",
                 "workspace-null",
                 {{"name", WireValue("")}, {"output", WireValue("")}, {"active_window_id", zero}}},
                {"Output",
                 "output-null",
                 {{"serial", WireValue("")},
                  {"physical_size", pair},
                  {"current_mode", zero},
                  {"logical", logical}}},
                {"WindowLayout",
                 "window-layout-null",
                 {{"pos_in_scrolling_layout", pair}, {"tile_pos_in_workspace_view", pair}}},
                {"ConfiguredMode", "configured-mode-null", {{"refresh", zero}}},
                {"Cast", "cast-null", {{"pid", zero}, {"pw_node_id", zero}}}};
        QVariantMap rows;
        for (const auto &record : records) {
            auto present = fixture(record.fixture);
            for (const auto &member : record.present)
                present = replaced(present, member.first, member.second);
            QVariant typed;
            DecodeError error;
            QVERIFY(decodeNamed(record.type, present, typed, error));
            rows.insert(record.type, typed);
            for (const auto &member : record.present) {
                auto omitted = *present.objectValue();
                omitted.removeIf([&](const auto &item) { return item.first == member.first; });
                QVariant none;
                QVERIFY(decodeNamed(record.type, *WireValue::object(omitted), none, error));
                const auto encoded = encodeVariant(none);
                QVERIFY(encoded);
                QCOMPARE(encoded->lookup(member.first)->type(), WireValue::Type::Null);
                const auto *meta = typed.metaType().metaObject();
                const auto property =
                        meta->property(meta->indexOfProperty(qPrintable(camel(member.first))));
                QVERIFY(property.readOnGadget(typed.constData()).metaType()
                        != QMetaType::fromType<std::nullptr_t>());
            }
        }
        QCOMPARE(rows["Window"].value<NiriWindow>().pid, std::optional<qint32>(0));
        QCOMPARE(rows["Output"].value<NiriOutput>().currentMode, std::optional<quint64>(0));
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("values", rows);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            QtObject {
                property bool passed: values.Window.title === "" && values.Window.appId === ""
                    && values.Window.pid === 0 && values.Window.workspaceId === "0"
                    && values.Window.focusTimestamp.secs === "0"
                    && values.Window.focusTimestamp.nanos === 0
                    && values.Workspace.name === "" && values.Workspace.output === ""
                    && values.Workspace.activeWindowId === "0"
                    && values.Output.serial === "" && values.Output.currentMode === "0"
                    && values.Output.physicalSize.length === 2 && values.Output.physicalSize[0] === 0
                    && values.Output.logical.width === 0 && values.Output.logical.scale === 0
                    && values.WindowLayout.posInScrollingLayout[0] === "0"
                    && values.WindowLayout.tilePosInWorkspaceView[0] === 0
                    && values.ConfiguredMode.refresh === 0
                    && values.Cast.pid === 0 && values.Cast.pwNodeId === 0
            }
        )",
                          QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QVERIFY(object->property("passed").toBool());
    }

    void constructorValidation()
    {
        QQmlEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import Niri as N
            QtObject {
                property int rejected: 0
                property bool passed: false
                property var failedReply
                property bool failedAfterReturn: false
                Component.onCompleted: {
                    const bad = [
                        () => N.NiriArgs.workspaceReferenceArgId("18446744073709551616"),
                        () => N.NiriArgs.workspaceReferenceArgId(18446744073709551616),
                        () => N.NiriArgs.workspaceReferenceArgId(-1),
                        () => N.NiriArgs.workspaceReferenceArgId(0.5),
                        () => N.NiriArgs.workspaceReferenceArgId(true),
                        () => N.NiriArgs.workspaceReferenceArgIndex(256),
                        () => N.NiriArgs.workspaceReferenceArgIndex("1"),
                        () => N.NiriArgs.sizeChangeSetFixed(2147483648),
                        () => N.NiriArgs.sizeChangeSetProportion(Infinity),
                        () => N.NiriArgs.configuredMode({width: 1}),
                        () => N.NiriArgs.configuredMode({width: 1, height: false}),
                        () => N.NiriArgs.castTargetNothing({future: true}),
                        () => N.NiriArgs.castTargetOutput({name: "x", typo: "y"}),
                        () => N.NiriArgs.output({}),
                        () => N.NiriArgs.configuredMode(undefined)
                        , () => { const value = {}; value.self = value; return N.NiriArgs.castTargetOutput(value); }
                    ];
                    for (const call of bad) {
                        const value = call();
                        if (value.valid === false && value.error.length > 0) rejected++;
                    }
                    const exact = N.NiriArgs.workspaceReferenceArgId(9223372036854775808);
                    passed = rejected === bad.length && exact.id === "9223372036854775808"
                        && N.NiriArgs.workspaceReferenceArgId(18446744073709549568).id === "18446744073709549568"
                        && N.NiriArgs.workspaceReferenceArgId(9007199254740994).id === "9007199254740994"
                        && N.NiriArgs.workspaceReferenceArgId("0").id === "0"
                        && N.NiriArgs.modeToSetSpecific(N.NiriArgs.configuredMode({width: 0, height: 0})).specific.width === 0
                        && N.NiriArgs.configuredMode({width: 65535, height: 0, refresh: null}).refresh === null;
                    const invalid = N.NiriArgs.sizeChangeSetFixed(2147483648);
                    passed = passed && invalid.kind === N.NiriSizeChange.Invalid && invalid.error.length > 0;
                    failedReply = N.NiriRequests.sendRaw(invalid);
                    const queued = !failedReply.completed;
                    failedReply.finished.connect(() => {
                        failedAfterReturn = queued && failedReply.isError
                            && failedReply.error.category === "Configuration";
                    });
                }
            }
        )",
                          QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("rejected").toInt(), 16);
        QVERIFY(object->property("passed").toBool());
        QTRY_VERIFY(object->property("failedAfterReturn").toBool());
        NiriSizeChange invalid;
        DecodeError error;
        QVERIFY(!NiriIpc::encode(invalid, &error));
        QCOMPARE(error.path, "$");
        QMap<QString, NiriTimestamp> map;
        QVERIFY(!decode(parsed("{\"a.b\":{\"secs\":0,\"nanos\":4294967296}}"), map, &error));
        QCOMPARE(error.path, "$['a.b']['nanos']");
        NiriOutput output;
        auto overflow =
                replaced(fixture("output-null"), "current_mode", parsed("18446744073709551616"));
        QVERIFY(!decode(overflow, output, &error));
        QCOMPARE(error.path, "$['current_mode']");
        NiriWindowLayout layout;
        QVERIFY(!decode(replaced(fixture("window-layout-null"), "pos_in_scrolling_layout",
                                 parsed("[0,18446744073709551616]")),
                        layout, &error));
        QCOMPARE(error.path, "$['pos_in_scrolling_layout'][1]");
    }

    void recordEventValues()
    {
        auto *events = NiriEvents::instance();
        QSignalSpy started(events, &NiriEvents::castStartedOrChanged);
        QSignalSpy casts(events, &NiriEvents::castsChanged);
        QSignalSpy timestamps(events, &NiriEvents::windowFocusTimestampChanged);
        QSignalSpy nativeTimestamps(events->nativeEvents(),
                                    &NiriNativeEvents::windowFocusTimestampChanged);
        auto cast = replaced(fixture("cast-present"), "kind", WireValue("FutureCast"));
        const auto epoch = NiriSession::instance()->epoch();
        events->dispatchWireEvent("CastStartedOrChanged", *WireValue::object({{"cast", cast}}),
                                  epoch);
        events->dispatchWireEvent(
                "CastsChanged", *WireValue::object({{"casts", WireValue(WireValue::Array{cast})}}),
                epoch);
        QCOMPARE(started.count(), 1);
        QCOMPARE(casts.count(), 1);
        const auto typed = started.first().first().value<NiriCast>();
        QCOMPARE(typed.streamId, std::numeric_limits<quint64>::max());
        QCOMPARE(typed.kind.kind(), NiriCastKind::Unknown);
        QCOMPARE(typed.kind.wireName(), "FutureCast");
        QCOMPARE(casts.first().first().toList().first().value<NiriCast>(), typed);
        events->dispatchWireEvent("WindowFocusTimestampChanged",
                                  *WireValue::object({{"id", parsed("18446744073709551615")},
                                                      {"focus_timestamp", WireValue()}}),
                                  epoch);
        QCOMPARE(timestamps.count(), 1);
        QCOMPARE(timestamps.first().first().toString(), "18446744073709551615");
        QCOMPARE(timestamps.first().at(1).metaType(), QMetaType::fromType<std::nullptr_t>());
        QCOMPARE(nativeTimestamps.first().first().value<quint64>(),
                 std::numeric_limits<quint64>::max());
        QVERIFY(!nativeTimestamps.first().at(1).value<std::optional<NiriTimestamp>>());
    }

    void socketConsumers_data() { exactIds_data(); }
    void socketConsumers()
    {
        QFETCH(QString, text);
        auto window = replaced(fixture("window-present"), "id", parsed(text.toLatin1()));
        window = replaced(window, "is_focused", WireValue(true));
        window = replaced(window, "title", WireValue());
        const auto windows = WireValue(WireValue::Array{window});
        CodecServer server;
        QVERIFY(server.listen());
        server.responses = {
                {"Windows", windows},
                {"Workspaces", WireValue(WireValue::Array{fixture("workspace-present")})},
                {"Outputs", *WireValue::object({{"DP-1", fixture("output-present")}})},
                {"FocusedWindow", window},
                {"FocusedOutput", WireValue()},
                {"KeyboardLayouts", fixture("keyboard-layouts")},
                {"Version", WireValue("26.04")}};
        server.initialEvents =
                *NiriWire::encode(*WireValue::object(
                        {{"WindowsChanged", *WireValue::object({{"windows", windows}})}}))
                + '\n';
        auto *session = NiriSession::instance();
        session->setAutoReconnect(false);
        session->connectToSocket(server.server.fullServerName());
        const auto stop = qScopeGuard([session] { session->stop(); });
        QQmlEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        engine.rootContext()->setContextProperty("expectedId", text);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import Niri as N
            QtObject {
                id: root
                property var reply
                property bool queryPassed: false
                property N.NiriWindow reactive: N.NiriWindow { windowId: expectedId }
                property N.NiriWindows windowModel: N.NiriWindows {}
                property Instantiator items: Instantiator {
                    model: root.windowModel
                    delegate: QtObject {
                        required property var window
                        property bool passed: window.id === expectedId && window.title === null
                    }
                }
                property bool streamPassed: reactive.valid && reactive.id === expectedId
                    && reactive.title === null && N.NiriState.focusedWindowId === expectedId
                    && N.NiriState.focusedWindowTitle === null
                    && items.count === 1 && items.objectAt(0).passed
                Component.onCompleted: {
                    reply = N.NiriRequests.windows();
                    reply.finished.connect(() => {
                        queryPassed = !reply.isError && reply.value.length === 1
                            && reply.value[0].id === expectedId && reply.value[0].title === null;
                    });
                }
            }
        )",
                          QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QTRY_VERIFY(object->property("queryPassed").toBool());
        QTRY_VERIFY(object->property("streamPassed").toBool());
        QVERIFY(server.stream);
        auto *requests = NiriRequests::instance();
        QList<std::function<NiriPendingReply *()>> queries = {
                [requests] { return requests->workspaces(); },
                [requests] { return requests->outputs(); },
                [requests] { return requests->focusedWindow(); },
                [requests] { return requests->focusedOutput(); },
                [requests] { return requests->keyboardLayouts(); },
                [requests] { return requests->version(); }};
        for (const auto &query : queries) {
            QScopedPointer<NiriPendingReply> reply(query());
            QSignalSpy finished(reply.get(), &NiriPendingReply::finished);
            QVERIFY(finished.wait());
            QVERIFY2(!reply->isError(), qPrintable(reply->error().message));
            QVERIFY(reply->value().isValid());
        }
        auto malformed = replaced(window, "pid", WireValue("bad"));
        server.responses["Windows"] = WireValue(WireValue::Array{malformed});
        QScopedPointer<NiriPendingReply> bad(requests->windows());
        QSignalSpy finished(bad.get(), &NiriPendingReply::finished);
        QVERIFY(finished.wait());
        QVERIFY(bad->isError());
        QCOMPARE(bad->error().fieldPath, "$.Ok.Windows[0]['pid']");
        QCOMPARE(bad->wireDocument()
                         .root.lookup("Ok")
                         ->lookup("Windows")
                         ->arrayValue()
                         ->first()
                         .lookup("id")
                         ->numberValue()
                         ->lexeme(),
                 text.toLatin1());
        auto *events = NiriEvents::instance();
        QSignalSpy errors(events, &NiriEvents::decodeError);
        QSignalSpy published(events, &NiriEvents::windowOpenedOrChanged);
        QSignalSpy raw(events, &NiriEvents::rawEvent);
        server.stream->write(
                *NiriWire::encode(*WireValue::object(
                        {{"WindowOpenedOrChanged", *WireValue::object({{"window", malformed}})}}))
                + '\n');
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(published.count(), 0);
        QCOMPARE(errors.first().first().toString(), "$['WindowOpenedOrChanged']['window']['pid']");
        QCOMPARE(raw.last().at(1).toMap()["window"].toMap()["id"].toString(), text);
        QVERIFY(object->property("streamPassed").toBool());
    }

    void malformed_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("path");
        QTest::newRow("missing") << QByteArray("{}") << QString("Timestamp")
                                 << QString("$['nanos']");
        QTest::newRow("uint32-overflow") << QByteArray("{\"secs\":0,\"nanos\":4294967296}")
                                         << QString("Timestamp") << QString("$['nanos']");
        QTest::newRow("negative") << QByteArray("{\"secs\":-1,\"nanos\":0}") << QString("Timestamp")
                                  << QString("$['secs']");
        QTest::newRow("u64-overflow") << QByteArray("{\"secs\":18446744073709551616,\"nanos\":0}")
                                      << QString("Timestamp") << QString("$['secs']");
        QTest::newRow("uint16-overflow") << QByteArray("{\"width\":65536,\"height\":0}")
                                         << QString("ConfiguredMode") << QString("$['width']");
        QTest::newRow("tuple-length")
                << QByteArray("{\"rgb\":[0,1]}") << QString("PickedColor") << QString("$['rgb']");
        QTest::newRow("known-union") << QByteArray("{\"SetFixed\":2147483648}")
                                     << QString("SizeChange") << QString("$['SetFixed']");
        QTest::newRow("uint8") << QByteArray("{\"Index\":256}") << QString("WorkspaceReferenceArg")
                               << QString("$['Index']");
        QTest::newRow("wrong-enum-kind")
                << QByteArray("42") << QString("Transform") << QString("$");
        QTest::newRow("unit-wrapper")
                << QByteArray("{\"Automatic\":null}") << QString("ModeToSet") << QString("$");
    }
    void malformed()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, type);
        QFETCH(QString, path);
        QVariant value;
        DecodeError error;
        QVERIFY(!decodeNamed(type, parsed(json), value, error));
        QCOMPARE(error.path, path);
        QVERIFY(!value.isValid());
    }
};

QTEST_MAIN(TestCodec)
#include "test_codec.moc"
