#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QVariant>
#include <QString>

#include "generated/niriipccodec_generated.h"

QByteArray toSnakeCase(QByteArrayView camel);

QVariant jsonToGadget(const QJsonObject &json, QMetaType targetType);

template <typename T>
T fromNiriJson(const QJsonObject &json)
{
    T value{};
    const auto wire = NiriWire::fromQJsonValueLossy(json);
    if (wire)
        NiriIpc::decode(*wire, value);
    return value;
}

QVariantList jsonArrayToGadgets(const QJsonArray &arr, QMetaType elementType);

int snakeCaseCacheSizeForTesting();
