#include "niriwire.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTest>
#include <limits>

using namespace NiriWire;

class TestWire : public QObject
{
    Q_OBJECT
private slots:
    void exactNumbers_data();
    void exactNumbers();
    void checkedIntegers_data();
    void checkedIntegers();
    void narrowRanges();
    void doubleRange();
    void tokenValidation();
    void invalidSyntax_data();
    void invalidSyntax();
    void unicodeAndEscapes();
    void invalidUtf8_data();
    void invalidUtf8();
    void constructedStrings();
    void stringLiteral();
    void orderedValuesAndPresence();
    void nestingLimit();
    void projection();
    void qmlProjection();
    void lossyBridges();
    void differentialStrings();
    void differentialSyntax();
};

void TestWire::stringLiteral()
{
    static_assert(!std::is_constructible_v<WireValue, int>);
    static_assert(!std::is_constructible_v<WireValue, void *>);
    const WireValue value("name");
    QCOMPARE(value.type(), WireValue::Type::String);
    QCOMPARE(*encode(value), QByteArray("\"name\""));
    QCOMPARE(*encode(WireValue(true)), QByteArray("true"));
    const bool disabled = false;
    QCOMPARE(*encode(WireValue(disabled)), QByteArray("false"));
}

void TestWire::exactNumbers_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<bool>("fitsUnsigned");
    const QList<QByteArray> values = {"0",
                                      "9007199254740991",
                                      "9007199254740992",
                                      "9007199254740993",
                                      "9223372036854775807",
                                      "9223372036854775808",
                                      "18446744073709551615",
                                      "18446744073709551616",
                                      "1234567890123456789012345678901234567890",
                                      "-1",
                                      "-9223372036854775808",
                                      "-9223372036854775809",
                                      "0.125",
                                      "-0.0",
                                      "1.25e+2",
                                      "1E9999"};
    for (qsizetype i = 0; i < values.size(); ++i)
        QTest::newRow(values[i].constData()) << values[i] << (i < 7 || i == 14);
}

void TestWire::exactNumbers()
{
    QFETCH(QByteArray, text);
    QFETCH(bool, fitsUnsigned);
    Error error;
    const auto doc = parse(text, &error);
    QVERIFY2(doc, qPrintable(error.reason));
    QCOMPARE(doc->bytes, text);
    QVERIFY(doc->root.numberValue());
    QCOMPARE(doc->root.numberValue()->lexeme(), text);
    QCOMPARE(doc->root.numberValue()->toUInt64().has_value(), fitsUnsigned);
    QCOMPARE(*encode(doc->root), text);
    const auto nested = parse("{\"id\":" + text + ",\"extra\":[" + text + "]}");
    QVERIFY(nested);
    QCOMPARE(*encode(nested->root), "{\"id\":" + text + ",\"extra\":[" + text + "]}");
    QCOMPARE(parse(*encode(nested->root))->root, nested->root);
}

void TestWire::checkedIntegers_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<bool>("fits");
    QTest::addColumn<qint64>("expected");
    QTest::newRow("signed-max") << QByteArray("9223372036854775807") << true
                                << std::numeric_limits<qint64>::max();
    QTest::newRow("signed-min") << QByteArray("-9223372036854775808") << true
                                << std::numeric_limits<qint64>::min();
    QTest::newRow("signed-over") << QByteArray("9223372036854775808") << false << qint64(0);
    QTest::newRow("signed-under") << QByteArray("-9223372036854775809") << false << qint64(0);
    QTest::newRow("fraction") << QByteArray("1.1") << false << qint64(0);
    QTest::newRow("tiny-fraction") << QByteArray("1e-99999999999999999999") << false << qint64(0);
    QTest::newRow("huge") << QByteArray("1e99999999999999999999") << false << qint64(0);
    QTest::newRow("exact-exponent") << QByteArray("-12.300e2") << true << qint64(-1230);
    QTest::newRow("exact-decimal") << QByteArray("42.0000") << true << qint64(42);
    QTest::newRow("fraction-not-rounded") << QByteArray("9007199254740993.1") << false << qint64(0);
    QTest::newRow("fraction-at-boundary")
            << QByteArray("9223372036854775807.0000001") << false << qint64(0);
    QTest::newRow("zero-large-exponent")
            << QByteArray("0e99999999999999999999") << true << qint64(0);
    QTest::newRow("negative-zero") << QByteArray("-0") << true << qint64(0);
    QTest::newRow("cancel-exponent")
            << QByteArray("100000000000000000000000000000000000e-35") << true << qint64(1);
}

void TestWire::checkedIntegers()
{
    QFETCH(QByteArray, text);
    QFETCH(bool, fits);
    QFETCH(qint64, expected);
    const auto token = NumberToken::fromText(text);
    QVERIFY(token);
    const auto value = token->toInt64();
    QCOMPARE(value.has_value(), fits);
    if (fits)
        QCOMPARE(*value, expected);
}

void TestWire::narrowRanges()
{
    auto token = [](const char *text) { return *NumberToken::fromText(text); };
    QCOMPARE(*token("255").toUInt8(), quint8(255));
    QVERIFY(!token("256").toUInt8());
    QCOMPARE(*token("65535").toUInt16(), quint16(65535));
    QVERIFY(!token("65536").toUInt16());
    QCOMPARE(*token("4294967295").toUInt32(), std::numeric_limits<quint32>::max());
    QVERIFY(!token("4294967296").toUInt32());
    QCOMPARE(*token("2147483647").toInt32(), std::numeric_limits<qint32>::max());
    QCOMPARE(*token("-2147483648").toInt32(), std::numeric_limits<qint32>::min());
    QVERIFY(!token("2147483648").toInt32());
    QVERIFY(!token("-2147483649").toInt32());
    QVERIFY(!token("-1").toUInt64());
    QVERIFY(!token("-0").toUInt64()); // Unsigned schema values reject a negative sign.
    QVERIFY(!token("255.1").toUInt8());
    QCOMPARE(*token("2.55e2").toUInt8(), quint8(255));
    QCOMPARE(*token("184467440737095516150e-1").toUInt64(), std::numeric_limits<quint64>::max());
    QVERIFY(!token("18446744073709551616.0").toUInt64());
    QCOMPARE(NumberToken::fromUInt64(std::numeric_limits<quint64>::max()).lexeme(),
             QByteArray("18446744073709551615"));
    QCOMPARE(NumberToken::fromInt64(std::numeric_limits<qint64>::min()).lexeme(),
             QByteArray("-9223372036854775808"));
}

void TestWire::doubleRange()
{
    QCOMPARE(*NumberToken::fromText("1.25e2")->toDouble(), 125.0);
    QCOMPARE(*NumberToken::fromText("-0.125")->toDouble(), -0.125);
    QVERIFY(!NumberToken::fromText("1e9999")->toDouble());
    QVERIFY(!NumberToken::fromText("1e-9999")->toDouble());
    QVERIFY(NumberToken::fromText("1e9999")->toNumber().isNull());
}

void TestWire::tokenValidation()
{
    Error error;
    for (const auto &text :
         {QByteArray("12.e-1"), QByteArray(" 1"), QByteArray("1 "), QByteArray("1\n"),
          QByteArray("1\r\n{}"), QByteArray("1,2"), QByteArray("1\0", 2)}) {
        QVERIFY(!NumberToken::fromText(text, &error));
        QVERIFY(!error.reason.isEmpty());
    }
    for (const auto &text :
         {QByteArray("1"), QByteArray("-0"), QByteArray("1.0"), QByteArray("1e+0")}) {
        const auto token = NumberToken::fromText(text);
        QVERIFY(token);
        QCOMPARE(*encode(WireValue(*token)), text);
    }
}

void TestWire::invalidSyntax_data()
{
    QTest::addColumn<QByteArray>("bytes");
    const QList<QByteArray> invalid = {"",
                                       " ",
                                       "01",
                                       "-01",
                                       "+1",
                                       "1.",
                                       ".5",
                                       "-",
                                       "1e",
                                       "1e+",
                                       "1e-",
                                       "NaN",
                                       "Infinity",
                                       "-Infinity",
                                       "1 2",
                                       "true false",
                                       "nullx",
                                       "{}garbage",
                                       "[]{}",
                                       "//comment\n0",
                                       "/*comment*/0",
                                       "[1,]",
                                       "{\"x\":1,}",
                                       "[",
                                       "{",
                                       "[1",
                                       "{\"x\":",
                                       "{x:1}",
                                       "{\"x\" 1}",
                                       "[1 2]",
                                       "{\"a\":1,\"a\":2}",
                                       "{\"a\":1,\"\\u0061\":2}",
                                       "\"\\uD800\"",
                                       "\"\\uDC00\"",
                                       "\"\\uD800\\u0041\"",
                                       "\"\\uDC00\\uD800\"",
                                       "\"\\u123\"",
                                       "\"\\uGGGG\"",
                                       "\"\\x\"",
                                       "\"abc",
                                       "\"abc\\",
                                       QByteArray("\"a\nb\""),
                                       QByteArray("\"a\0b\"", 5),
                                       "\"\\U0000\"",
                                       "[}",
                                       "{]",
                                       "[null,,null]",
                                       "\xef\xbb\xbf{}"};
    for (qsizetype i = 0; i < invalid.size(); ++i)
        QTest::newRow(qPrintable(QString::number(i))) << invalid[i];
}

void TestWire::invalidSyntax()
{
    QFETCH(QByteArray, bytes);
    Error error;
    QVERIFY(!parse(bytes, &error));
    QVERIFY(!error.reason.isEmpty());
    QVERIFY(error.offset >= 0 && error.offset <= bytes.size());
    // Public numeric text cannot inject a second value, whitespace or a line.
    QVERIFY(!NumberToken::fromText(bytes, &error));
}

void TestWire::unicodeAndEscapes()
{
    const QByteArray source = "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0000\\u20ac\\uD83D\\uDE00\"";
    const auto doc = parse(source);
    QVERIFY(doc);
    const QString expected = QString::fromUtf8("\"\\/\b\f\n\r\t") + QChar(0)
            + QString::fromUtf8("\xe2\x82\xac\xf0\x9f\x98\x80");
    QCOMPARE(*doc->root.stringValue(), expected);
    const auto encoded = encode(doc->root);
    QVERIFY(encoded);
    QVERIFY(!encoded->contains('\n'));
    QVERIFY(!encoded->contains('\r'));
    QVERIFY(!encoded->contains('\0'));
    QCOMPARE(parse(*encoded)->root, doc->root);
    const QByteArray raw = "\"\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80\xef\xbf\xbf"
                           "\xf0\x90\x80\x80\xf4\x8f\xbf\xbf\"";
    const auto rawDoc = parse(raw);
    QVERIFY(rawDoc);
    QCOMPARE(*encode(rawDoc->root), raw);
    for (int control = 0; control < 32; ++control) {
        const auto text = QString(QChar(control));
        const auto bytes = encode(WireValue(text));
        QVERIFY(bytes);
        QCOMPARE(*parse(*bytes)->root.stringValue(), text);
        QVERIFY(!bytes->contains(char(control)));
    }
}

void TestWire::invalidUtf8_data()
{
    QTest::addColumn<QByteArray>("bytes");
    const QList<QByteArray> hex = {"80",       "bf",     "c080",   "c1bf",  "c2",       "c241",
                                   "e08080",   "eda080", "edbfbf", "e282",  "f0808080", "f4908080",
                                   "f5808080", "fe",     "ff",     "f09f98"};
    for (const auto &value : hex)
        QTest::newRow(value.constData()) << ('"' + QByteArray::fromHex(value) + '"');
}
void TestWire::invalidUtf8()
{
    QFETCH(QByteArray, bytes);
    Error error;
    QVERIFY(!parse(bytes, &error));
    QVERIFY(error.reason.contains("UTF-8"));
    QVERIFY(error.offset >= 1 && error.offset < bytes.size());
}

void TestWire::constructedStrings()
{
    Error error;
    QVERIFY(!encode(WireValue(QString(QChar(0xd800))), &error));
    QVERIFY(error.reason.contains("UTF-16"));
    QVERIFY(!encode(WireValue(QString(QChar(0xdc00))), &error));
    auto invalidKey = WireValue::object({{QString(QChar(0xd800)), WireValue()}});
    QVERIFY(invalidKey);
    QVERIFY(!encode(*invalidKey, &error));
    auto duplicate = WireValue::object({{"x", WireValue()}, {"x", WireValue(true)}}, &error);
    QVERIFY(!duplicate);
    QVERIFY(error.reason.contains("duplicate"));
}

void TestWire::orderedValuesAndPresence()
{
    Error error;
    const QByteArray bytes = " \n {\"z\":null,\"a\":false,\"b\":[true,\"\",0,{}]}\r\t";
    auto doc = parse(bytes, &error);
    QVERIFY2(doc, qPrintable(error.reason));
    QCOMPARE(doc->bytes, bytes);
    QVERIFY(doc->root.lookup("z"));
    QCOMPARE(doc->root.lookup("z")->type(), WireValue::Type::Null);
    QVERIFY(!doc->root.lookup("missing"));
    QCOMPARE(*doc->root.lookup("a")->boolValue(), false);
    QVERIFY(!doc->root.lookup("a")->numberValue());
    const auto copy = doc->root;
    doc->root = WireValue();
    QCOMPARE(copy.objectValue()->at(0).first, QString("z"));
    QCOMPARE(copy.objectValue()->at(1).first, QString("a"));
    QCOMPARE(*encode(copy), QByteArray("{\"z\":null,\"a\":false,\"b\":[true,\"\",0,{}]}"));
    const auto offsetDoc = parse("\"\xc3\xa9\" x", &error);
    QVERIFY(!offsetDoc);
    QCOMPARE(error.offset, qsizetype(5)); // Byte offset, not UTF-16 character count.
    QVERIFY(!parse("{\"a\":1,\"a\":2}", &error));
    QCOMPARE(error.offset, qsizetype(7));
}

void TestWire::nestingLimit()
{
    Error error;
    QVERIFY(parse("0", &error, {0}));
    QVERIFY(!parse("[]", &error, {0}));
    QVERIFY(parse("[{}]", &error, {2}));
    QVERIFY(!parse("[{}]", &error, {1}));
    QCOMPARE(error.offset, qsizetype(1));
    QVERIFY(error.reason.contains("depth"));
    const QByteArray deep = QByteArray(1000, '[') + "0" + QByteArray(1000, ']');
    QVERIFY(!parse(deep, &error));
    QCOMPARE(error.offset, qsizetype(128));
    const auto accepted = parse(deep, &error, {1000});
    QVERIFY2(accepted, qPrintable(error.reason));
    QCOMPARE(*encode(accepted->root), deep);
    QVERIFY(!parse("null", &error, {-1}));
}

void TestWire::projection()
{
    const auto doc = parse("{\"id\":18446744073709551615,\"small\":1,\"negative\":-1,\"no\":null,"
                           "\"yes\":true,\"s\":\"x\",\"a\":[1.25e2]}");
    QVERIFY(doc);
    const auto map = toQmlProjection(doc->root).toMap();
    QCOMPARE(map["id"].metaType(), QMetaType::fromType<QString>());
    QCOMPARE(map["id"].toString(), QString("18446744073709551615"));
    QCOMPARE(map["small"].toString(), QString("1"));
    QCOMPARE(map["negative"].toString(), QString("-1"));
    QCOMPARE(map["no"].metaType(), QMetaType::fromType<std::nullptr_t>());
    QCOMPARE(map["yes"].metaType(), QMetaType::fromType<bool>());
    QCOMPARE(map["s"].toString(), QString("x"));
    const auto number = map["a"].toList().first().value<NumberToken>();
    QCOMPARE(number.text(), QString("1.25e2"));
    QCOMPARE(number.toNumber().toDouble(), 125.0);
}

void TestWire::qmlProjection()
{
    QQmlEngine engine;
    const auto doc = parse("{\"id\":18446744073709551615,\"small\":0,\"no\":null,\"yes\":true,"
                           "\"s\":\"x\",\"n\":1.25e2}");
    engine.rootContext()->setContextProperty("wireProjection", toQmlProjection(doc->root));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        import Niri
        QtObject {
            property bool passed: wireProjection.id === "18446744073709551615"
                && wireProjection.small === "0" && wireProjection.no === null
                && wireProjection.yes === true && wireProjection.s === "x"
                && wireProjection.n.text === "1.25e2" && wireProjection.n.toNumber() === 125
        }
    )",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QVERIFY(object->property("passed").toBool());
}

void TestWire::lossyBridges()
{
    const auto doc = parse("{\"a\":[null,true,\"x\",1.25],\"b\":42}");
    const auto json = toQJsonValueLossy(doc->root);
    QVERIFY(json);
    const auto back = fromQJsonValueLossy(*json);
    QVERIFY(back);
    QCOMPARE(*encode(*back), *encode(doc->root));
    QVERIFY(fromQVariantLossy(QVariantMap{{"a", true}}));
    QVERIFY(!fromQJsonValueLossy(QJsonValue(QJsonValue::Undefined)));
    QVERIFY(!toQJsonValueLossy(parse("1e9999")->root));
    // The authoritative token survives even when the explicit Qt projection rounds.
    const auto large = parse("18446744073709551615");
    QVERIFY(toQJsonValueLossy(large->root));
    QCOMPARE(large->root.numberValue()->lexeme(), QByteArray("18446744073709551615"));
}

void TestWire::differentialStrings()
{
    QRandomGenerator random(0x4e495249);
    for (int trial = 0; trial < 1000; ++trial) {
        QString text;
        const int length = int(random.bounded(80));
        for (int i = 0; i < length; ++i) {
            const char32_t scalar = random.bounded(0x110000);
            if (scalar >= 0xd800 && scalar <= 0xdfff)
                continue;
            text += QString::fromUcs4(&scalar, 1);
        }
        const WireValue original(WireValue::Array{
                WireValue(text), WireValue(NumberToken::fromUInt64(random.generate64()))});
        const auto bytes = encode(original);
        QVERIFY(bytes);
        const auto doc = parse(*bytes);
        QVERIFY(doc);
        QCOMPARE(doc->root, original);
        QJsonParseError error;
        const auto qt = QJsonDocument::fromJson(*bytes, &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(qt.array()[0].toString(), text);
        // Independent Qt string encoder, parsed by our strict reader.
        const auto qtBytes = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
        const auto ours = parse(qtBytes);
        QVERIFY(ours);
        QCOMPARE(*ours->root.arrayValue()->first().stringValue(), text);
    }
}

void TestWire::differentialSyntax()
{
    // Independent RFC lexical regex + Qt structural acceptance oracle. Qt's
    // scanner alone accepts e.g. 12.e-1 and unescaped control bytes in strings.
    // Replace validated number lexemes by zero before Qt's structural check so
    // its double range cannot reject our deliberately arbitrary exact tokens.
    // Omit object names (our duplicate-name policy is intentionally stricter).
    const QRegularExpression tokens(QStringLiteral(
            R"json([ \t\r\n]+|true|false|null|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?|"(?:[^"\\\x00-\x1f]|\\["\\/bfnrt]|\\u[0-9a-fA-F]{4})*"|[\[\]{},:])json"));
    QVERIFY(tokens.isValid());
    QRandomGenerator random(0x8259);
    const QByteArray alphabet = "[],: \t\n\r0123-.e+trufalsn\"/\\";
    const QList<QByteArray> seeds = {
            "null", "true", "false", "12.3e-1", "[0,true,null,\"s\"]", "[[1],[],\"\\n\"]"};
    int valid = 0;
    int invalid = 0;
    for (int trial = 0; trial < 5000; ++trial) {
        auto candidate = seeds[random.bounded(quint32(seeds.size()))];
        const int mutations = int(random.bounded(4));
        for (int change = 0; change < mutations; ++change) {
            const auto index = qsizetype(random.bounded(quint32(candidate.size() + 1)));
            const auto ch = alphabet[random.bounded(quint32(alphabet.size()))];
            if (random.bounded(2) && index < candidate.size())
                candidate.remove(index, 1);
            else
                candidate.insert(index, ch);
        }
        qsizetype covered = 0;
        QByteArray normalized;
        auto matches = tokens.globalMatch(QString::fromLatin1(candidate));
        while (matches.hasNext()) {
            const auto match = matches.next();
            if (match.capturedStart() != covered)
                break;
            covered = match.capturedEnd();
            const auto token = match.captured().toLatin1();
            const char first = token[0];
            normalized += first == '-' || (first >= '0' && first <= '9') ? QByteArray("0") : token;
        }
        QJsonParseError error;
        const auto qt = QJsonDocument::fromJson('[' + normalized + ']', &error);
        const bool qtAccepted = error.error == QJsonParseError::NoError && qt.array().size() == 1;
        const bool referenceAccepted = qtAccepted && covered == candidate.size();
        const auto ours = parse(candidate);
        QVERIFY2(ours.has_value() == referenceAccepted, candidate.constData());
        if (ours) {
            ++valid;
            QCOMPARE(parse(*encode(ours->root))->root, ours->root);
        } else {
            ++invalid;
        }
    }
    QVERIFY(valid > 1000);
    QVERIFY(invalid > 1000);
}

QTEST_MAIN(TestWire)
#include "test_wire.moc"
