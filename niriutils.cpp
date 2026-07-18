#include "niriutils.h"

#include <QDebug>
#include <QJsonValue>
#include <QMetaProperty>
#include <QMetaType>

#include "niritypes.h"

QByteArray toSnakeCase(QByteArrayView camel)
{
    QByteArray result;
    result.reserve(camel.size() + 4);
    for (int i = 0; i < camel.size(); ++i) {
        char c = camel[i];
        if (c >= 'A' && c <= 'Z') {
            if (i > 0)
                result.append('_');
            result.append(c - 'A' + 'a');
        } else {
            result.append(c);
        }
    }
    return result;
}

static QVariant jsonValueToVariant(const QJsonValue &val, QMetaType targetType);

// Dispatch table for list-of-gadget types. Add a branch when introducing
// a new QList<Gadget> property.
template<typename T>
static QVariant typedListFromJson(const QJsonArray &arr)
{
    QList<T> list;
    list.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        QJsonObject obj = v.toObject();
        list.append(fromNiriJson<T>(obj));
    }
    return QVariant::fromValue(list);
}

using ListConverter = QVariant (*)(const QJsonArray &);
static ListConverter findListConverter(QMetaType containerType)
{
#define BRIDGE(T) +[](const QJsonArray &a) { return typedListFromJson<T>(a); }
    static const struct { int containerTypeId; ListConverter fn; } table[] = {
        { QMetaType::fromType<QList<NiriMode>>().id(), BRIDGE(NiriMode) },
    };
#undef BRIDGE
    for (const auto &entry : table) {
        if (entry.containerTypeId == containerType.id())
            return entry.fn;
    }
    return nullptr;
}

static QVariant jsonArrayToTypedList(const QJsonArray &arr, QMetaType containerType)
{
    QByteArray typeName = containerType.name();

    if (typeName == "QList<int>") {
        QList<int> list;
        list.reserve(arr.size());
        for (const QJsonValue &v : arr)
            list.append(v.toInt());
        return QVariant::fromValue(list);
    }
    if (typeName == "QList<double>") {
        QList<double> list;
        list.reserve(arr.size());
        for (const QJsonValue &v : arr)
            list.append(v.toDouble());
        return QVariant::fromValue(list);
    }
    if (typeName == "QStringList") {
        QStringList list;
        list.reserve(arr.size());
        for (const QJsonValue &v : arr)
            list.append(v.toString());
        return QVariant::fromValue(list);
    }

    if (auto fn = findListConverter(containerType))
        return fn(arr);

    qWarning() << "niriqml: unsupported list type" << typeName;
    return arr.toVariantList();
}

static QVariant jsonValueToVariant(const QJsonValue &val, QMetaType targetType)
{
    if (val.isNull())
        return {};

    if (val.isObject()) {
        const QMetaObject *mo = targetType.metaObject();
        if (mo)
            return jsonToGadget(val.toObject(), targetType);
        return val.toVariant();
    }

    if (val.isArray())
        return jsonArrayToTypedList(val.toArray(), targetType);

    QVariant v = val.toVariant();
    v.convert(targetType);
    return v;
}

QVariant jsonToGadget(const QJsonObject &json, QMetaType targetType)
{
    const QMetaObject *meta = targetType.metaObject();
    if (!meta) {
        qWarning() << "niriqml: jsonToGadget called on non-gadget type" << targetType.name();
        return {};
    }

    QVariant result(targetType);
    void *instance = result.data();
    QByteArray fullTypeName(targetType.name());

    for (int i = 0; i < meta->propertyCount(); ++i) {
        QMetaProperty prop = meta->property(i);
        if (!prop.isWritable())
            continue;

        QByteArray snakeKey = toSnakeCase(prop.name());
        QJsonValue val = json.value(QString::fromUtf8(snakeKey));
        if (val.isNull())
            continue;

        QMetaType propMt = prop.metaType();
        QVariant converted = jsonValueToVariant(val, propMt);

        if (converted.isValid()) {
            if (!prop.writeOnGadget(instance, converted)) {
                qWarning() << "niriqml: writeOnGadget failed for" << prop.name()
                           << "(type:" << propMt.name() << ") in" << fullTypeName;
            }
        }
    }

    return result;
}

QVariantList jsonArrayToGadgets(const QJsonArray &arr, QMetaType elementType)
{
    QVariantList result;
    result.reserve(arr.size());
    for (const QJsonValue &v : arr)
        result.append(jsonToGadget(v.toObject(), elementType));
    return result;
}

