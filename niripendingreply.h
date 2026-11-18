#pragma once

#include "nirierror.h"
#include "niriwire.h"
#include <QObject>
#include <QPointer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class NiriRequestOperation;

class NiriPendingReply : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriPendingReply)
    QML_UNCREATABLE("Use NiriRequests or NiriActions")
    Q_PROPERTY(bool completed READ completed NOTIFY finished)
    Q_PROPERTY(Status status READ status NOTIFY finished)
    Q_PROPERTY(bool isError READ isError NOTIFY finished)
    Q_PROPERTY(NiriError error READ error NOTIFY finished)
    Q_PROPERTY(QVariant value READ value NOTIFY finished)
    Q_PROPERTY(QVariant document READ document NOTIFY finished)
    Q_PROPERTY(QString json READ json NOTIFY finished)
    Q_PROPERTY(QString responseKind READ responseKind NOTIFY finished)
    Q_PROPERTY(QString executionCertainty READ executionCertainty NOTIFY finished)
    Q_PROPERTY(QString endpoint READ endpoint CONSTANT)
    Q_PROPERTY(QString endpointRevision READ endpointRevision CONSTANT)
    Q_PROPERTY(QString requestId READ requestId CONSTANT)
public:
    enum Status { Pending, Succeeded, Failed };
    Q_ENUM(Status)
    explicit NiriPendingReply(QObject *parent = nullptr);
    ~NiriPendingReply() override;
    bool completed() const { return m_status != Pending; }
    Status status() const { return m_status; }
    bool isError() const { return m_status == Failed; }
    NiriError error() const { return m_error; }
    virtual QVariant value() const { return m_value; }
    QVariant document() const { return NiriWire::toQmlProjection(m_document.root); }
    QString json() const { return QString::fromUtf8(m_document.bytes); }
    const NiriWire::Document &wireDocument() const { return m_document; }
    QString responseKind() const { return m_responseKind; }
    QString executionCertainty() const { return m_certainty; }
    QString endpoint() const { return m_endpoint; }
    QString endpointRevision() const { return m_revision; }
    QString requestId() const { return m_id; }
    Q_INVOKABLE void cancel();
signals:
    void finished();

protected:
    const QVariant &nativeValue() const { return m_value; }

private:
    friend class NiriRequestOperation;
    void complete(QVariant value, NiriError error, NiriWire::Document document,
                  QString responseKind, QString certainty);
    QPointer<NiriRequestOperation> m_operation;
    Status m_status = Pending;
    NiriError m_error;
    QVariant m_value;
    NiriWire::Document m_document;
    QString m_responseKind;
    QString m_certainty = QStringLiteral("NotSent");
    QString m_endpoint, m_revision, m_id;
};
