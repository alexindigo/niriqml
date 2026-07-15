#include "nirievents.h"

#include <QQmlEngine>
#include <QDebug>

NiriEvents *NiriEvents::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

NiriEvents *NiriEvents::instance()
{
    static NiriEvents s;
    return &s;
}

NiriEvents::NiriEvents(QObject *parent)
    : QObject(parent)
{
}

void NiriEvents::dispatchEvent(const QString &name, const QVariantMap &payload)
{
    emit rawEvent(name, payload);
}
