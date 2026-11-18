#pragma once

#include "niritypes.h"
#include <functional>

namespace NiriPeerInfoReader {
// socklen_t is unsigned int on Linux. The seam also keeps platform headers out
// of public Qt headers. A failed call follows getsockopt's errno contract.
using GetSocketOption = std::function<int(qintptr, int, int, void *, unsigned int *)>;
PeerInfo read(qintptr descriptor, GetSocketOption getOption = {});
} // namespace NiriPeerInfoReader
