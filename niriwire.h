#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>
#include <type_traits>

namespace NiriWire {

struct Error
{
    qsizetype offset = 0; // Byte offset in the input, not a UTF-16 index.
    QString reason;
};

// A validated JSON decimal lexeme, never a floating-point storage slot.
class NumberToken
{
    Q_GADGET
    QML_VALUE_TYPE(niriJsonNumber)
    Q_PROPERTY(QString text READ text CONSTANT)
public:
    NumberToken() = default; // Valid zero, including when constructed by Qt.
    static std::optional<NumberToken> fromText(const QByteArray &text, Error *error = nullptr);
    static NumberToken fromUInt64(quint64 value);
    static NumberToken fromInt64(qint64 value);
    const QByteArray &lexeme() const { return m_text; }
    QString text() const { return QString::fromLatin1(m_text); }
    bool isIntegerToken() const;
    std::optional<quint64> toUInt64() const;
    std::optional<qint64> toInt64() const;
    std::optional<quint32> toUInt32() const;
    std::optional<qint32> toInt32() const;
    std::optional<quint8> toUInt8() const;
    std::optional<quint16> toUInt16() const;
    // Explicit floating-point projection; decimal values can round to binary64.
    // Overflow/underflow and non-finite results fail rather than becoming zero/inf.
    std::optional<double> toDouble() const;
    Q_INVOKABLE QVariant toNumber() const;
    bool operator==(const NumberToken &other) const { return m_text == other.m_text; }

private:
    explicit NumberToken(QByteArray text) : m_text(std::move(text)) { }
    QByteArray m_text = "0";
};

class WireValue
{
    struct BoolTag
    {
    };

public:
    enum class Type { Null, Bool, String, NumberToken, Array, Object };
    using Array = QList<WireValue>;
    using Object = QList<QPair<QString, WireValue>>; // Insertion order is retained.

    WireValue(); // Null
    template <typename T, std::enable_if_t<std::is_same_v<T, bool>, int> = 0>
    explicit WireValue(T value) : WireValue(value, BoolTag{})
    {
    }
    explicit WireValue(const char *value) : WireValue(QString::fromUtf8(value)) { }
    explicit WireValue(QString value);
    explicit WireValue(NumberToken value);
    explicit WireValue(Array value);
    // Duplicate names are rejected for constructed values as well as parsed ones.
    static std::optional<WireValue> object(Object members, Error *error = nullptr);

    Type type() const;
    std::optional<bool> boolValue() const;
    const QString *stringValue() const;
    const NumberToken *numberValue() const;
    const Array *arrayValue() const;
    const Object *objectValue() const;
    // nullptr means Missing; a present JSON null is a non-null WireValue pointer.
    const WireValue *lookup(const QString &key) const;
    bool operator==(const WireValue &other) const;

private:
    struct Data;
    WireValue(bool value, BoolTag);
    explicit WireValue(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> m_data; // No mutable tree or shared mutable cache.
};

struct Document
{
    QByteArray bytes; // Original root bytes, including insignificant whitespace.
    WireValue root;
};

struct ParseOptions
{
    qsizetype maxDepth = 128; // Container nesting; scalar roots have depth zero.
};

std::optional<Document> parse(const QByteArray &bytes, Error *error = nullptr,
                              ParseOptions options = {});
// Compact JSON, no LF framing delimiter. Rejects invalid UTF-16 in constructed strings.
std::optional<QByteArray> encode(const WireValue &value, Error *error = nullptr);

// Explicitly lossy compatibility bridges. QJsonValue has already lost arbitrary
// number lexemes and cannot represent the entire u64 range faithfully. These
// functions are projections, never authoritative storage or a typed decode path.
std::optional<QJsonValue> toQJsonValueLossy(const WireValue &value, Error *error = nullptr);
std::optional<WireValue> fromQJsonValueLossy(const QJsonValue &value, Error *error = nullptr);
std::optional<WireValue> fromQVariantLossy(const QVariant &value, Error *error = nullptr);
// Every integer *token* becomes a decimal QString (also small/negative integers).
// Other number tokens remain exact niriJsonNumber gadgets with explicit toNumber().
// Bool/string/null keep their types, arrays/maps keep their structure. Object order
// is not retained by QVariantMap. This projection is not an inverse wire codec.
QVariant toQmlProjection(const WireValue &value);

} // namespace NiriWire

Q_DECLARE_METATYPE(NiriWire::NumberToken)
