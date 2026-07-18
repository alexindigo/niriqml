#include "niripendingreply.h"

NiriPendingReply::NiriPendingReply(QObject *parent) : QObject(parent) { }

bool NiriPendingReply::isError() const
{
    return m_isError;
}
NiriError NiriPendingReply::error() const
{
    return m_error;
}
QVariant NiriPendingReply::value() const
{
    return m_value;
}

void NiriPendingReply::setFinished(const QVariant &value)
{
    m_isError = false;
    m_value = value;
    emit finished();
}

void NiriPendingReply::setError(const NiriError &error)
{
    m_isError = true;
    m_error = error;
    emit finished();
}
