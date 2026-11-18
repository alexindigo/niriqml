#include "niripeerinfo.h"

#include <cerrno>
#include <limits>
#ifdef Q_OS_LINUX
#  include <sys/socket.h>
#  include <sys/types.h>
#endif

PeerInfo NiriPeerInfoReader::read(qintptr descriptor, GetSocketOption getOption)
{
    PeerInfo info;
#ifdef Q_OS_LINUX
    if (!getOption) {
        getOption = [](qintptr fd, int level, int option, void *value, unsigned int *size) {
            return ::getsockopt(int(fd), level, option, value, size);
        };
    }
    struct ucred credentials{};
    unsigned int size = sizeof(credentials);
    if (getOption(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0
        && size == sizeof(credentials)) {
        info.credentialsAvailable = true;
        info.pid = credentials.pid;
        info.uid = credentials.uid;
        info.gid = credentials.gid;
    } else {
        info.credentialsError = errno;
    }
    QByteArray buffer(256, '\0');
    for (;;) {
        size = static_cast<unsigned int>(buffer.size());
        if (getOption(descriptor, SOL_SOCKET, SO_PEERSEC, buffer.data(), &size) == 0) {
            if (size > static_cast<unsigned int>(buffer.size())) {
                info.securityContextError = ERANGE;
                break;
            }
            buffer.resize(size);
            if (buffer.endsWith('\0'))
                buffer.chop(1);
            info.securityContextAvailable = true;
            info.peerSecurityContextBytes = buffer;
            // Linux does not specify label encoding; QML gets a UTF-8 display
            // projection with replacement characters, C++ retains the bytes.
            info.peerSecurityContext = QString::fromUtf8(buffer);
            break;
        }
        const int error = errno;
        if (error != ERANGE || size <= static_cast<unsigned int>(buffer.size())
            || size > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
            info.securityContextError = error;
            break;
        }
        buffer.resize(size);
    }
#else
    Q_UNUSED(descriptor)
    Q_UNUSED(getOption)
#endif
    return info;
}
