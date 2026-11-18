#include "niripendingreply.h"
#include "nirirequestoperation.h"

NiriPendingReply::NiriPendingReply(QObject *parent) : QObject(parent) { }

NiriPendingReply::~NiriPendingReply()
{
    if (m_operation)
        m_operation->cancel();
}

void NiriPendingReply::cancel()
{
    if (!NiriSession::checkThread(this))
        return;
    if (m_operation)
        m_operation->cancel();
}

void NiriPendingReply::complete(QVariant value, NiriError error, NiriWire::Document document,
                                QString responseKind, QString certainty)
{
    if (completed())
        return;
    m_status = error.category.isEmpty() ? Succeeded : Failed;
    m_value = std::move(value);
    m_error = std::move(error);
    m_document = std::move(document);
    m_responseKind = std::move(responseKind);
    m_certainty = std::move(certainty);
    m_operation = nullptr;
    emit finished(); // A handler may delete this. Do not access members afterward.
}
