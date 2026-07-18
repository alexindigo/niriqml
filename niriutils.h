#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QVariant>
#include <QString>

QByteArray toSnakeCase(QByteArrayView camel);

QVariant jsonToGadget(const QJsonObject &json, QMetaType targetType);

template <typename T>
T fromNiriJson(const QJsonObject &json)
{
    return jsonToGadget(json, QMetaType::fromType<T>()).template value<T>();
}

QVariantList jsonArrayToGadgets(const QJsonArray &arr, QMetaType elementType);
