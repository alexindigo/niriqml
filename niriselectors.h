#pragma once

#include "nirieventstreamstate.h"

namespace NiriSelectors {
NiriEventStreamState::Entry<NiriWindow>
window(const NiriEventStreamState &state, std::optional<quint64> id,
       const NiriEventStreamState::Changes *before = nullptr);
NiriEventStreamState::Entry<NiriWorkspace>
workspace(const NiriEventStreamState &state, const QString &name, std::optional<quint64> id,
          const NiriEventStreamState::Changes *before = nullptr);
} // namespace NiriSelectors
