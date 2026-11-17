#include "niriactions.h"
#include "nirirequests.h"
#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
#include <QMetaMethod>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <limits>

using namespace NiriWire;

namespace {
WireValue loadFixture(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open fixture");
    const auto document = parse(file.readAll());
    if (!document)
        qFatal("Invalid fixture JSON");
    return document->root;
}

// JS fixture input is deliberately independent of the callable generator.
// Wide integers become strings before entering JS; all small numbers stay numeric.
QVariant jsInput(const WireValue &wire)
{
    if (wire.type() == WireValue::Type::Null)
        return QVariant::fromValue(nullptr);
    if (wire.stringValue())
        return *wire.stringValue();
    if (wire.boolValue())
        return *wire.boolValue();
    if (const auto *number = wire.numberValue()) {
        if (const auto integer = number->toUInt64(); integer && *integer > 9007199254740991ULL)
            return QString::number(*integer);
        return number->toDouble().value();
    }
    if (const auto *array = wire.arrayValue()) {
        QVariantList result;
        for (const auto &item : *array)
            result.append(jsInput(item));
        return result;
    }
    QVariantMap result;
    for (const auto &member : *wire.objectValue())
        result.insert(member.first, jsInput(member.second));
    return result;
}

// Canonical byte comparison preserves full unsigned integer values. Floating
// lexemes normalize to the same binary64 value, not through QJson's double tree.
WireValue canonical(const WireValue &wire)
{
    if (const auto *number = wire.numberValue()) {
        if (const auto value = number->toUInt64())
            return WireValue(NumberToken::fromUInt64(*value));
        if (const auto value = number->toInt64())
            return WireValue(NumberToken::fromInt64(*value));
        return WireValue(
                *NumberToken::fromText(QByteArray::number(number->toDouble().value(), 'g', 17)));
    }
    if (const auto *array = wire.arrayValue()) {
        WireValue::Array result;
        for (const auto &item : *array)
            result.append(canonical(item));
        return WireValue(result);
    }
    if (const auto *object = wire.objectValue()) {
        WireValue::Object result;
        for (const auto &member : *object)
            result.append({member.first, canonical(member.second)});
        std::sort(result.begin(), result.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        return *WireValue::object(result);
    }
    return wire;
}

class ActionServer
{
public:
    ActionServer()
    {
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            while (auto *peer = server.nextPendingConnection()) {
                ++connections;
                auto buffer = std::make_shared<NiriLineBuffer>();
                QObject::connect(peer, &QLocalSocket::readyRead, &server, [this, peer, buffer] {
                    buffer->feed(peer->readAll(), [this, peer](const QByteArray &line) {
                        arrivals.append(line);
                        peer->write(response);
                    });
                });
            }
        });
    }
    bool listen() { return directory.isValid() && server.listen(directory.filePath("socket")); }
    QTemporaryDir directory;
    QLocalServer server;
    QList<QByteArray> arrivals;
    QByteArray response = "{\"Ok\":\"Handled\"}\n";
    int connections = 0;
};
} // namespace

class TestGeneratedActions : public QObject
{
    Q_OBJECT
    WireValue vectors;
    WireValue manifest;
    const WireValue *binding(const QString &tag) const
    {
        for (const auto &row : *manifest.arrayValue()) {
            if (*row.lookup("variant")->stringValue() == tag)
                return &row;
        }
        return nullptr;
    }
    static std::unique_ptr<QObject> consumer(QQmlEngine &engine)
    {
        engine.addImportPath(QCoreApplication::applicationDirPath());
        QQmlComponent component(&engine);
        component.setData(
                "import QtQml\nimport Niri 1.0\nQtObject { "
                "property NiriPendingReply retained: null; "
                "function submit(name, args) { "
                "retained = NiriActions[name].apply(NiriActions, args); } "
                "function typed() { retained = NiriActions.moveWindowToWorkspace("
                "'18446744073709551615', NiriArgs.workspaceReferenceArgId('456'), false); } }",
                QUrl());
        auto root = std::unique_ptr<QObject>(component.create());
        if (!root)
            qWarning() << component.errors();
        return root;
    }
private slots:
    void initTestCase()
    {
        vectors = loadFixture(QFINDTESTDATA("fixtures/serde-v26.04/vectors.json"));
        manifest = loadFixture(QFINDTESTDATA("../generated/ipc-actions.json"));
    }
    void init()
    {
        NiriSession::instance()->stop();
        NiriSession::instance()->selectEndpoint(QString());
    }
    void inventory()
    {
        QCOMPARE(manifest.arrayValue()->size(), 141);
        QSet<QByteArray> expected;
        for (const auto &entry : *manifest.arrayValue())
            expected.insert(entry.lookup("method")->stringValue()->toLatin1());
        QSet<QByteArray> actual;
        const auto &meta = NiriIpcActions::staticMetaObject;
        for (int i = meta.methodOffset(); i < meta.methodCount(); ++i)
            actual.insert(meta.method(i).name());
        QCOMPARE(actual, expected);
    }
    void everyAction_data()
    {
        QTest::addColumn<QString>("name");
        for (const auto &row : *vectors.lookup("surfaceVectors")->arrayValue()) {
            if (*row.lookup("type")->stringValue() == "Action") {
                const auto name = *row.lookup("name")->stringValue();
                QTest::newRow(qPrintable(name)) << name;
            }
        }
    }
    void everyAction()
    {
        QFETCH(QString, name);
        const WireValue *expected = nullptr;
        for (const auto &row : *vectors.lookup("surfaceVectors")->arrayValue()) {
            if (*row.lookup("name")->stringValue() == name)
                expected = row.lookup("value");
        }
        QVERIFY(expected);
        const auto &variant = expected->objectValue()->first();
        const auto *entry = binding(variant.first);
        QVERIFY(entry);
        QVariantList arguments;
        for (const auto &arg : *entry->lookup("arguments")->arrayValue()) {
            const auto *field = variant.second.lookup(*arg.lookup("wire")->stringValue());
            QVERIFY(field);
            arguments.append(jsInput(*field));
        }
        ActionServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        QQmlEngine engine;
        auto root = consumer(engine);
        QVERIFY(root);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "submit",
                                          Q_ARG(QVariant, *entry->lookup("method")->stringValue()),
                                          Q_ARG(QVariant, arguments)));
        auto *reply = root->property("retained").value<NiriPendingReply *>();
        QVERIFY(reply);
        QVERIFY(!reply->completed());
        QTRY_VERIFY(reply->completed());
        QVERIFY2(!reply->isError(), qPrintable(reply->error().message));
        QCOMPARE(reply->value().toString(), "Handled");
        QCOMPARE(server.connections, 1);
        QCOMPARE(server.arrivals.size(), 1);
        const auto received = parse(server.arrivals.first());
        QVERIFY(received);
        const auto request = *WireValue::object({{"Action", *expected}});
        QCOMPARE(NiriWire::encode(canonical(received->root)), NiriWire::encode(canonical(request)));
    }
    void typedArgumentQml()
    {
        ActionServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        QQmlEngine engine;
        auto root = consumer(engine);
        QVERIFY(root);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "typed"));
        auto *reply = root->property("retained").value<NiriPendingReply *>();
        QVERIFY(reply);
        QTRY_VERIFY(reply->completed());
        QVERIFY2(!reply->isError(), qPrintable(reply->error().message));
        QCOMPARE(server.arrivals.size(), 1);
        const auto received = parse(server.arrivals.first());
        QVERIFY(received);
        const auto *body = received->root.lookup("Action")->lookup("MoveWindowToWorkspace");
        QCOMPARE(body->lookup("window_id")->numberValue()->toUInt64(),
                 std::optional<quint64>(std::numeric_limits<quint64>::max()));
        QCOMPARE(body->lookup("reference")->lookup("Id")->numberValue()->toUInt64(),
                 std::optional<quint64>(456));
        QCOMPARE(*body->lookup("focus")->boolValue(), false);
    }
    void invalidArguments_data()
    {
        QTest::addColumn<QString>("method");
        QTest::addColumn<QVariantList>("args");
        const auto null = QVariant::fromValue(nullptr);
        QTest::newRow("unknown-union-field")
                << QString("setWindowWidth")
                << QVariantList{null, QVariantMap{{"SetFixed", 800}, {"typo", false}}};
        QTest::newRow("wrong-boolean") << QString("moveWindowToWorkspace")
                                       << QVariantList{null, QVariantMap{{"Id", "456"}}, "false"};
        QTest::newRow("required-id-null") << QString("focusWindow") << QVariantList{null};
        QTest::newRow("id-overflow")
                << QString("closeWindow") << QVariantList{"18446744073709551616"};
        QTest::newRow("uint8-overflow") << QString("focusWindowInColumn") << QVariantList{256};
        QTest::newRow("missing-required-id") << QString("focusWindow") << QVariantList{};
        QTest::newRow("missing-required-boolean") << QString("moveWindowToWorkspace")
                                                  << QVariantList{null, QVariantMap{{"Id", "456"}}};
    }
    void omittedOptionalId()
    {
        ActionServer server;
        QVERIFY(server.listen());
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        QQmlEngine engine;
        auto root = consumer(engine);
        QVERIFY(root);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "submit",
                                          Q_ARG(QVariant, QString("closeWindow")),
                                          Q_ARG(QVariant, QVariantList{})));
        auto *reply = root->property("retained").value<NiriPendingReply *>();
        QVERIFY(reply);
        QTRY_VERIFY(reply->completed());
        QVERIFY(!reply->isError());
        QCOMPARE(server.arrivals.size(), 1);
        const auto document = parse(server.arrivals.first());
        QVERIFY(document);
        QCOMPARE(document->root.lookup("Action")->lookup("CloseWindow")->lookup("id")->type(),
                 WireValue::Type::Null);
    }
    void wrongResponse()
    {
        ActionServer server;
        QVERIFY(server.listen());
        server.response = "{\"Ok\":{\"Version\":\"unexpected\"}}\n";
        NiriSession::instance()->selectEndpoint(server.server.fullServerName());
        std::unique_ptr<NiriPendingReply> reply(NiriActions::instance()->closeWindow(std::nullopt));
        QVERIFY(reply);
        QTRY_VERIFY(reply->completed());
        QVERIFY(reply->isError());
        QCOMPARE(reply->error().category, "Decode");
        QCOMPARE(reply->error().fieldPath, "$.Ok");
        QCOMPARE(reply->wireDocument().bytes, server.response.trimmed());
    }
    void invalidArguments()
    {
        QFETCH(QString, method);
        QFETCH(QVariantList, args);
        ActionServer server;
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

QTEST_MAIN(TestGeneratedActions)
#include "test_generated_actions.moc"
