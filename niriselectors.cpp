#include "niriselectors.h"

NiriEventStreamState::Entry<NiriWindow>
NiriSelectors::window(const NiriEventStreamState &state, std::optional<quint64> id,
                      const NiriEventStreamState::Changes *before)
{
    if (id && before && before->windows.contains(*id))
        return before->windows.value(*id).before;
    return id ? state.window(*id) : nullptr;
}

NiriEventStreamState::Entry<NiriWorkspace>
NiriSelectors::workspace(const NiriEventStreamState &state, const QString &name,
                         std::optional<quint64> id, const NiriEventStreamState::Changes *before)
{
    if (!name.isEmpty())
        return state.workspaceByName(name, before);
    if (id && before && before->workspaces.contains(*id))
        return before->workspaces.value(*id).before;
    return id ? state.workspace(*id) : nullptr;
}
