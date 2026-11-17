#pragma once

#include "niriwire.h"
#include <QFile>
#include <QTest>
#include <algorithm>

namespace NiriSurfaceTest {
using namespace NiriWire;
inline WireValue loadFixture(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open fixture");
    const auto document = parse(file.readAll());
    if (!document)
        qFatal("Invalid fixture JSON");
    return document->root;
}

// Preserve arbitrary u64s before entering JS; small numeric inputs stay numeric.
inline QVariant jsInput(const WireValue &wire)
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

inline WireValue canonical(const WireValue &wire)
{
    if (const auto *number = wire.numberValue()) {
        if (const auto value = number->toUInt64())
            return WireValue(NumberToken::fromUInt64(*value));
        if (const auto value = number->toInt64())
            return WireValue(NumberToken::fromInt64(*value));
        if (number->isIntegerToken())
            return wire; // Never round an out-of-range integer through binary64.
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
} // namespace NiriSurfaceTest
