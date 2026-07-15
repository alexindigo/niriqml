#include "nirirequests.h"

NiriRequests *NiriRequests::instance()
{
    static NiriRequests s;
    return &s;
}

NiriRequests::NiriRequests(QObject *parent)
    : QObject(parent)
{
}

void NiriRequests::send(const QJsonObject &request, Callback callback)
{
    Q_UNUSED(request)
    Q_UNUSED(callback)
}
