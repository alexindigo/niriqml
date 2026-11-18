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
    Q_PROPERTY(QString category MEMBER category)
    Q_PROPERTY(QString certainty MEMBER certainty)
    Q_PROPERTY(QString requestId MEMBER requestId)
    Q_PROPERTY(QString endpoint MEMBER endpoint)
    Q_PROPERTY(QString endpointRevision MEMBER endpointRevision)
    Q_PROPERTY(QString phase MEMBER phase)
    Q_PROPERTY(QString fieldPath MEMBER fieldPath)
    Q_PROPERTY(qint64 byteOffset MEMBER byteOffset)
    Q_PROPERTY(int socketError MEMBER socketError)

public:
    int code = 0;
    QString message;
    QString category;
    QString certainty = QStringLiteral("NotSent");
    QString requestId;
    QString endpoint;
    QString endpointRevision;
    QString phase;
    QString fieldPath;
    qint64 byteOffset = -1;
    int socketError = -1;
};
