#pragma once

#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "nirierror.h"

class NiriPendingReply : public QObject
{
    Q_OBJECT
    QML_UNCREATABLE("Use NiriActions methods instead")

    Q_PROPERTY(bool isError READ isError NOTIFY finished)
    Q_PROPERTY(NiriError error READ error NOTIFY finished)
    Q_PROPERTY(QVariant value READ value NOTIFY finished)

public:
    explicit NiriPendingReply(QObject *parent = nullptr);

    bool isError() const;
    NiriError error() const;
    QVariant value() const;

    void setFinished(const QVariant &value);
    void setError(const NiriError &error);

signals:
    void finished();

private:
    bool m_isError = false;
    NiriError m_error;
    QVariant m_value;
};
