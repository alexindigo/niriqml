#include "niriwire.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <limits>
#include <variant>

namespace NiriWire {
namespace {

bool fail(Error *error, qsizetype offset, const char *reason)
{
    if (error)
        *error = {offset, QString::fromLatin1(reason)};
    return false;
}

bool digit(char ch)
{
    return ch >= '0' && ch <= '9';
}
bool whitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

// Stops at the first byte not in this number. The caller owns delimiter/root checks.
bool numberEnd(const QByteArray &text, qsizetype &pos, Error *error)
{
    const auto size = text.size();
    if (pos < size && text[pos] == '-')
        ++pos;
    if (pos == size || !digit(text[pos]))
        return fail(error, pos, "expected number digit");
    if (text[pos] == '0') {
        ++pos;
        if (pos < size && digit(text[pos]))
            return fail(error, pos, "leading zero in number");
    } else {
        while (pos < size && digit(text[pos]))
            ++pos;
    }
    if (pos < size && text[pos] == '.') {
        ++pos;
        if (pos == size || !digit(text[pos]))
            return fail(error, pos, "expected fraction digit");
        while (pos < size && digit(text[pos]))
            ++pos;
    }
    if (pos < size && (text[pos] == 'e' || text[pos] == 'E')) {
        ++pos;
        if (pos < size && (text[pos] == '+' || text[pos] == '-'))
            ++pos;
        if (pos == size || !digit(text[pos]))
            return fail(error, pos, "expected exponent digit");
        while (pos < size && digit(text[pos]))
            ++pos;
    }
    return true;
}

// Exact base-10 arithmetic, never a double conversion. Exponents saturate only
// after they exceed any possible significand length plus the target's 20 digits.
std::optional<quint64> magnitude(const QByteArray &text)
{
    QByteArray digits;
    qsizetype pos = text.startsWith('-') ? 1 : 0;
    qsizetype fractional = 0;
    bool afterPoint = false;
    while (pos < text.size() && text[pos] != 'e' && text[pos] != 'E') {
        if (text[pos] == '.') {
            afterPoint = true;
        } else {
            digits += text[pos];
            if (afterPoint)
                ++fractional;
        }
        ++pos;
    }
    qsizetype first = 0;
    while (first < digits.size() && digits[first] == '0')
        ++first;
    if (first == digits.size())
        return quint64(0);
    digits.remove(0, first);
    qint64 exponent = 0;
    bool negativeExponent = false;
    const qint64 cap = text.size() > std::numeric_limits<qint64>::max() - 32
            ? std::numeric_limits<qint64>::max()
            : text.size() + 32;
    if (pos < text.size()) {
        ++pos;
        if (text[pos] == '+' || text[pos] == '-') {
            negativeExponent = text[pos] == '-';
            ++pos;
        }
        for (; pos < text.size(); ++pos) {
            const int d = text[pos] - '0';
            exponent = exponent > (cap - d) / 10 ? cap : exponent * 10 + d;
        }
    }
    if (negativeExponent)
        exponent = -exponent;
    // Avoid signed overflow even for a physically unallocatable input size.
    if (exponent < -std::numeric_limits<qint64>::max() + fractional)
        return std::nullopt;
    qint64 shift = exponent - fractional;
    while (shift < 0 && digits.endsWith('0')) {
        digits.chop(1);
        ++shift;
    }
    if (shift < 0 || shift > 20 || digits.size() > 20 - shift)
        return std::nullopt;
    digits.append(QByteArray(qsizetype(shift), '0'));
    bool ok = false;
    const quint64 value = digits.toULongLong(&ok, 10);
    return ok ? std::optional<quint64>(value) : std::nullopt;
}

template <typename T>
std::optional<T> narrow(std::optional<quint64> value)
{
    if (!value || *value > std::numeric_limits<T>::max())
        return std::nullopt;
    return T(*value);
}

class Parser
{
public:
    Parser(const QByteArray &bytes, Error *error, ParseOptions options)
        : bytes(bytes), error(error), options(options)
    {
    }

    std::optional<WireValue> run()
    {
        if (options.maxDepth < 0) {
            fail(error, 0, "negative nesting limit");
            return std::nullopt;
        }
        // Explicit stack: even a caller-selected large depth cannot overflow the
        // parser's C++ call stack. No recursive descent through containers.
        QList<Frame> stack;
        std::optional<WireValue> ready;
        bool needValue = true;
        while (true) {
            if (needValue) {
                skipSpace();
                if (pos == bytes.size())
                    return rejected("expected JSON value");
                const char ch = bytes[pos];
                if (ch == '[' || ch == '{') {
                    if (stack.size() >= options.maxDepth)
                        return rejected("maximum nesting depth exceeded");
                    ++pos;
                    stack.append(Frame(ch == '{'));
                    needValue = false;
                } else if (ch == '"') {
                    auto value = string();
                    if (!value)
                        return std::nullopt;
                    ready = WireValue(std::move(*value));
                } else if (ch == '-' || digit(ch)) {
                    const auto start = pos;
                    if (!numberEnd(bytes, pos, error))
                        return std::nullopt;
                    ready = WireValue(*NumberToken::fromText(bytes.mid(start, pos - start)));
                } else {
                    const QByteArray literal = ch == 't' ? "true" : ch == 'f' ? "false" : "null";
                    if (bytes.mid(pos, literal.size()) != literal)
                        return rejected("invalid JSON value");
                    pos += literal.size();
                    ready = ch == 't'   ? WireValue(true)
                            : ch == 'f' ? WireValue(false)
                                        : WireValue();
                }
            }
            if (ready) {
                if (stack.isEmpty()) {
                    skipSpace();
                    if (pos != bytes.size())
                        return rejected("trailing input after JSON root");
                    return ready;
                }
                auto &frame = stack.last();
                if (frame.object)
                    frame.members.append({std::move(frame.key), std::move(*ready)});
                else
                    frame.items.append(std::move(*ready));
                ready.reset();
                frame.state = State::Separator;
            }
            auto &frame = stack.last();
            skipSpace();
            if (pos == bytes.size())
                return rejected("unterminated container");
            const char end = frame.object ? '}' : ']';
            if (bytes[pos] == end
                && (frame.state == State::First || frame.state == State::Separator)) {
                ++pos;
                ready = frame.object ? WireValue::object(std::move(frame.members))
                                     : std::optional<WireValue>(WireValue(std::move(frame.items)));
                stack.removeLast();
                needValue = false;
                continue;
            }
            if (frame.state == State::Separator) {
                if (bytes[pos] != ',')
                    return rejected("expected comma or closing delimiter");
                ++pos;
                frame.state = State::Next;
                skipSpace();
            }
            if (frame.object) {
                const auto keyStart = pos;
                auto key = string();
                if (!key)
                    return std::nullopt;
                if (frame.keys.contains(*key)) {
                    fail(error, keyStart, "duplicate object key");
                    return std::nullopt;
                }
                frame.keys.insert(*key);
                frame.key = std::move(*key);
                skipSpace();
                if (pos == bytes.size() || bytes[pos] != ':')
                    return rejected("expected colon after object key");
                ++pos;
            }
            needValue = true;
        }
    }

private:
    enum class State { First, Next, Separator };
    struct Frame
    {
        explicit Frame(bool object) : object(object) { }
        bool object;
        State state = State::First;
        WireValue::Array items;
        WireValue::Object members;
        QSet<QString> keys;
        QString key;
    };
    const QByteArray &bytes;
    Error *error;
    ParseOptions options;
    qsizetype pos = 0;

    std::optional<WireValue> rejected(const char *reason)
    {
        fail(error, pos, reason);
        return std::nullopt;
    }
    void skipSpace()
    {
        while (pos < bytes.size() && whitespace(bytes[pos]))
            ++pos;
    }
    std::optional<ushort> hex4()
    {
        ushort result = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos == bytes.size()) {
                fail(error, pos, "incomplete Unicode escape");
                return std::nullopt;
            }
            const char ch = bytes[pos++];
            const int d = digit(ch)          ? ch - '0'
                    : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                    : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                             : -1;
            if (d < 0) {
                fail(error, pos - 1, "invalid Unicode escape");
                return std::nullopt;
            }
            result = ushort(result * 16 + d);
        }
        return result;
    }
    std::optional<QString> string()
    {
        if (pos == bytes.size() || bytes[pos] != '"') {
            fail(error, pos, "expected quoted string");
            return std::nullopt;
        }
        ++pos;
        QString result;
        while (pos < bytes.size()) {
            const auto start = pos;
            const auto ch = static_cast<unsigned char>(bytes[pos++]);
            if (ch == '"')
                return result;
            if (ch < 0x20) {
                fail(error, start, "unescaped control character");
                return std::nullopt;
            }
            if (ch == '\\') {
                if (pos == bytes.size())
                    break;
                const char escaped = bytes[pos++];
                switch (escaped) {
                case '"':
                    result += QLatin1Char('"');
                    break;
                case '\\':
                    result += QLatin1Char('\\');
                    break;
                case '/':
                    result += QLatin1Char('/');
                    break;
                case 'b':
                    result += QLatin1Char('\b');
                    break;
                case 'f':
                    result += QLatin1Char('\f');
                    break;
                case 'n':
                    result += QLatin1Char('\n');
                    break;
                case 'r':
                    result += QLatin1Char('\r');
                    break;
                case 't':
                    result += QLatin1Char('\t');
                    break;
                case 'u': {
                    auto first = hex4();
                    if (!first)
                        return std::nullopt;
                    if (QChar::isLowSurrogate(*first)) {
                        fail(error, start, "lone low surrogate");
                        return std::nullopt;
                    }
                    result += QChar(*first);
                    if (QChar::isHighSurrogate(*first)) {
                        if (bytes.mid(pos, 2) != "\\u") {
                            fail(error, pos, "missing low surrogate");
                            return std::nullopt;
                        }
                        pos += 2;
                        const auto secondStart = pos;
                        auto second = hex4();
                        if (!second)
                            return std::nullopt;
                        if (!QChar::isLowSurrogate(*second)) {
                            fail(error, secondStart, "invalid low surrogate");
                            return std::nullopt;
                        }
                        result += QChar(*second);
                    }
                    break;
                }
                default:
                    fail(error, pos - 1, "invalid string escape");
                    return std::nullopt;
                }
            } else if (ch < 0x80) {
                result += QChar(ch);
            } else {
                const int count = ch >= 0xc2 && ch <= 0xdf ? 1
                        : ch >= 0xe0 && ch <= 0xef         ? 2
                        : ch >= 0xf0 && ch <= 0xf4         ? 3
                                                           : -1;
                if (count < 0) {
                    fail(error, start, "invalid UTF-8 leading byte");
                    return std::nullopt;
                }
                char32_t code = ch & (0x7f >> (count + 1));
                for (int i = 0; i < count; ++i) {
                    if (pos == bytes.size()
                        || (static_cast<unsigned char>(bytes[pos]) & 0xc0) != 0x80) {
                        fail(error, pos, "invalid UTF-8 continuation");
                        return std::nullopt;
                    }
                    code = (code << 6) | (static_cast<unsigned char>(bytes[pos++]) & 0x3f);
                }
                if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800)
                    || (count == 3 && code < 0x10000) || code > 0x10ffff
                    || (code >= 0xd800 && code <= 0xdfff)) {
                    fail(error, start, "invalid UTF-8 scalar");
                    return std::nullopt;
                }
                if (code < 0x10000)
                    result += QChar(ushort(code));
                else {
                    result += QChar(QChar::highSurrogate(code));
                    result += QChar(QChar::lowSurrogate(code));
                }
            }
        }
        fail(error, pos, "unterminated string");
        return std::nullopt;
    }
};

bool quote(const QString &text, QByteArray &out, Error *error)
{
    out += '"';
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto ch = text[i].unicode();
        if (QChar::isSurrogate(ch)) {
            if (!QChar::isHighSurrogate(ch) || i + 1 == text.size()
                || !text[i + 1].isLowSurrogate())
                return fail(error, out.size(), "invalid UTF-16 in constructed string");
            out += text.mid(i, 2).toUtf8();
            ++i;
        } else if (ch == '"' || ch == '\\') {
            out += '\\';
            out += char(ch);
        } else if (ch < 0x20) {
            out += "\\u00";
            const char *hex = "0123456789abcdef";
            out += hex[ch >> 4];
            out += hex[ch & 15];
        } else {
            out += QString(text[i]).toUtf8();
        }
    }
    out += '"';
    return true;
}

} // namespace

struct WireValue::Data
{
    using Value = std::variant<std::monostate, bool, QString, NumberToken, Array, Object>;
    explicit Data(Value value = {}) : value(std::move(value)) { }
    const Value value;
};

WireValue::WireValue() : m_data(std::make_shared<const Data>()) { }
WireValue::WireValue(bool value, BoolTag) : m_data(std::make_shared<const Data>(value)) { }
WireValue::WireValue(QString value) : m_data(std::make_shared<const Data>(std::move(value))) { }
WireValue::WireValue(NumberToken value) : m_data(std::make_shared<const Data>(std::move(value))) { }
WireValue::WireValue(Array value) : m_data(std::make_shared<const Data>(std::move(value))) { }
WireValue::WireValue(std::shared_ptr<const Data> data) : m_data(std::move(data)) { }

std::optional<WireValue> WireValue::object(Object members, Error *error)
{
    QSet<QString> seen;
    for (const auto &member : members) {
        if (seen.contains(member.first)) {
            fail(error, 0, "duplicate object key");
            return std::nullopt;
        }
        seen.insert(member.first);
    }
    return WireValue(std::make_shared<const Data>(std::move(members)));
}

WireValue::Type WireValue::type() const
{
    return Type(m_data->value.index());
}
std::optional<bool> WireValue::boolValue() const
{
    const auto *value = std::get_if<bool>(&m_data->value);
    return value ? std::optional<bool>(*value) : std::nullopt;
}
const QString *WireValue::stringValue() const
{
    return std::get_if<QString>(&m_data->value);
}
const NumberToken *WireValue::numberValue() const
{
    return std::get_if<NumberToken>(&m_data->value);
}
const WireValue::Array *WireValue::arrayValue() const
{
    return std::get_if<Array>(&m_data->value);
}
const WireValue::Object *WireValue::objectValue() const
{
    return std::get_if<Object>(&m_data->value);
}
const WireValue *WireValue::lookup(const QString &key) const
{
    if (auto members = objectValue()) {
        for (const auto &member : *members) {
            if (member.first == key)
                return &member.second;
        }
    }
    return nullptr;
}
bool WireValue::operator==(const WireValue &other) const
{
    return m_data == other.m_data || m_data->value == other.m_data->value;
}

std::optional<NumberToken> NumberToken::fromText(const QByteArray &text, Error *error)
{
    qsizetype pos = 0;
    if (!numberEnd(text, pos, error))
        return std::nullopt;
    if (pos != text.size()) {
        fail(error, pos, "trailing input after number");
        return std::nullopt;
    }
    return NumberToken(text);
}
NumberToken NumberToken::fromUInt64(quint64 value)
{
    return NumberToken(QByteArray::number(value));
}
NumberToken NumberToken::fromInt64(qint64 value)
{
    return NumberToken(QByteArray::number(value));
}
bool NumberToken::isIntegerToken() const
{
    return !m_text.contains('.') && !m_text.contains('e') && !m_text.contains('E');
}
std::optional<quint64> NumberToken::toUInt64() const
{
    if (m_text.startsWith('-'))
        return std::nullopt;
    return magnitude(m_text);
}
std::optional<qint64> NumberToken::toInt64() const
{
    const auto value = magnitude(m_text);
    if (!value)
        return std::nullopt;
    const bool negative = m_text.startsWith('-');
    const quint64 max = quint64(std::numeric_limits<qint64>::max());
    if (*value > max + quint64(negative))
        return std::nullopt;
    if (negative && *value == max + 1)
        return std::numeric_limits<qint64>::min();
    return negative ? -qint64(*value) : qint64(*value);
}
std::optional<quint32> NumberToken::toUInt32() const
{
    return narrow<quint32>(toUInt64());
}
std::optional<quint16> NumberToken::toUInt16() const
{
    return narrow<quint16>(toUInt64());
}
std::optional<quint8> NumberToken::toUInt8() const
{
    return narrow<quint8>(toUInt64());
}
std::optional<qint32> NumberToken::toInt32() const
{
    const auto value = toInt64();
    if (!value || *value < std::numeric_limits<qint32>::min()
        || *value > std::numeric_limits<qint32>::max())
        return std::nullopt;
    return qint32(*value);
}
std::optional<double> NumberToken::toDouble() const
{
    bool ok = false;
    const double value = m_text.toDouble(&ok);
    return ok && std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}
QVariant NumberToken::toNumber() const
{
    const auto value = toDouble();
    return value ? QVariant(*value) : QVariant::fromValue(nullptr);
}

std::optional<Document> parse(const QByteArray &bytes, Error *error, ParseOptions options)
{
    if (error)
        *error = {};
    try {
        auto root = Parser(bytes, error, options).run();
        if (root)
            return Document{bytes, std::move(*root)};
    } catch (const std::bad_alloc &) {
        fail(error, 0, "allocation failure while parsing JSON");
    }
    return std::nullopt;
}

std::optional<QByteArray> encode(const WireValue &value, Error *error)
{
    if (error)
        *error = {};
    try {
        struct Frame
        {
            const WireValue *value;
            qsizetype next = -1;
        };
        QList<Frame> stack{{&value}};
        QByteArray out;
        while (!stack.isEmpty()) {
            auto &frame = stack.last();
            const auto &current = *frame.value;
            if (const auto *items = current.arrayValue()) {
                if (frame.next < 0) {
                    out += '[';
                    frame.next = 0;
                }
                if (frame.next == items->size()) {
                    out += ']';
                    stack.removeLast();
                    continue;
                }
                if (frame.next)
                    out += ',';
                const auto *child = &items->at(frame.next++);
                stack.append({child});
            } else if (const auto *members = current.objectValue()) {
                if (frame.next < 0) {
                    out += '{';
                    frame.next = 0;
                }
                if (frame.next == members->size()) {
                    out += '}';
                    stack.removeLast();
                    continue;
                }
                if (frame.next)
                    out += ',';
                const auto &member = members->at(frame.next++);
                if (!quote(member.first, out, error))
                    return std::nullopt;
                out += ':';
                stack.append({&member.second});
            } else {
                switch (current.type()) {
                case WireValue::Type::Null:
                    out += "null";
                    break;
                case WireValue::Type::Bool:
                    out += *current.boolValue() ? "true" : "false";
                    break;
                case WireValue::Type::String:
                    if (!quote(*current.stringValue(), out, error))
                        return std::nullopt;
                    break;
                case WireValue::Type::NumberToken:
                    out += current.numberValue()->lexeme();
                    break;
                default:
                    break;
                }
                stack.removeLast();
            }
        }
        return out;
    } catch (const std::bad_alloc &) {
        fail(error, 0, "allocation failure while encoding JSON");
        return std::nullopt;
    }
}

std::optional<QJsonValue> toQJsonValueLossy(const WireValue &value, Error *error)
{
    auto bytes = encode(value, error);
    if (!bytes)
        return std::nullopt;
    QJsonParseError qtError;
    const auto doc = QJsonDocument::fromJson('[' + *bytes + ']', &qtError);
    if (qtError.error != QJsonParseError::NoError) {
        if (error)
            *error = {qMax<qsizetype>(0, qtError.offset - 1), qtError.errorString()};
        return std::nullopt;
    }
    return doc.array().at(0);
}
std::optional<WireValue> fromQJsonValueLossy(const QJsonValue &value, Error *error)
{
    if (value.isUndefined()) {
        fail(error, 0, "undefined QJsonValue is not JSON");
        return std::nullopt;
    }
    auto bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    auto doc = parse(bytes.mid(1, bytes.size() - 2), error);
    return doc ? std::optional<WireValue>(std::move(doc->root)) : std::nullopt;
}
std::optional<WireValue> fromQVariantLossy(const QVariant &value, Error *error)
{
    return fromQJsonValueLossy(QJsonValue::fromVariant(value), error);
}
QVariant toQmlProjection(const WireValue &value)
{
    switch (value.type()) {
    case WireValue::Type::Null:
        return QVariant::fromValue(nullptr);
    case WireValue::Type::Bool:
        return *value.boolValue();
    case WireValue::Type::String:
        return *value.stringValue();
    case WireValue::Type::NumberToken:
        return value.numberValue()->isIntegerToken() ? QVariant(value.numberValue()->text())
                                                     : QVariant::fromValue(*value.numberValue());
    case WireValue::Type::Array: {
        QVariantList items;
        for (const auto &item : *value.arrayValue())
            items.append(toQmlProjection(item));
        return items;
    }
    case WireValue::Type::Object: {
        QVariantMap members;
        for (const auto &member : *value.objectValue())
            members.insert(member.first, toQmlProjection(member.second));
        return members;
    }
    }
    return {};
}

} // namespace NiriWire
