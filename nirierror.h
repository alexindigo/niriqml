#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

struct NiriError
{
    Q_GADGET
    QML_VALUE_TYPE(niriError)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(int code MEMBER code)
    Q_PROPERTY(QString message MEMBER message)

public:
    int code = 0;
    QString message;
};
