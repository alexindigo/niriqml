#pragma once

#include "generated/niriipctypes_generated.h"

// Peer credentials belong to niriqml's local socket, not niri-ipc's schema.
struct PeerInfo
{
    Q_GADGET
    QML_VALUE_TYPE(peerInfo)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(qint64 pid MEMBER pid)
    Q_PROPERTY(quint32 uid MEMBER uid)
    Q_PROPERTY(quint32 gid MEMBER gid)
    Q_PROPERTY(QString peerSecurityContext MEMBER peerSecurityContext)
    Q_PROPERTY(bool credentialsAvailable MEMBER credentialsAvailable)
    Q_PROPERTY(bool securityContextAvailable MEMBER securityContextAvailable)
    Q_PROPERTY(int credentialsError MEMBER credentialsError)
    Q_PROPERTY(int securityContextError MEMBER securityContextError)

public:
    qint64 pid = -1;
    quint32 uid = 0;
    quint32 gid = 0;
    QString peerSecurityContext;
    QByteArray peerSecurityContextBytes;
    bool credentialsAvailable = false;
    bool securityContextAvailable = false;
    int credentialsError = 0;
    int securityContextError = 0;

    bool operator==(const PeerInfo &o) const
    {
        return pid == o.pid && uid == o.uid && gid == o.gid
                && peerSecurityContext == o.peerSecurityContext
                && peerSecurityContextBytes == o.peerSecurityContextBytes
                && credentialsAvailable == o.credentialsAvailable
                && securityContextAvailable == o.securityContextAvailable
                && credentialsError == o.credentialsError
                && securityContextError == o.securityContextError;
    }
    bool operator!=(const PeerInfo &o) const { return !(*this == o); }
};
