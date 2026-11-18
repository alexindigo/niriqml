#pragma once

#include "generated/niriipctypes_generated.h"
#include <QMap>
#include <QSet>
#include <memory>

// Native session-owned port of niri v26.04 niri-ipc/src/state.rs plus the
// optional touchpad part from pinned main 689c1e9.
// Immutable entries and touched before-images support Qt row transitions without
// cloning the entire state on each delta. No observer runs inside apply/reset.
class NiriEventStreamState
{
public:
    template <typename T>
    using Entry = std::shared_ptr<const T>;
    template <typename T>
    struct RecordChange
    {
        Entry<T> before, after;
        quint64 fields = 0;
    };
    enum WindowField : quint64 {
        AppId = 1 << 0,
        FocusTimestamp = 1 << 1,
        Floating = 1 << 2,
        WindowFocused = 1 << 3,
        WindowUrgent = 1 << 4,
        Layout = 1 << 5,
        WindowPid = 1 << 6,
        Title = 1 << 7,
        WorkspaceId = 1 << 8,
        WindowExtensions = 1 << 9,
        AllWindowFields = (1 << 10) - 1
    };
    enum WorkspaceField : quint64 {
        ActiveWindowId = 1 << 0,
        Index = 1 << 1,
        Active = 1 << 2,
        WorkspaceFocused = 1 << 3,
        WorkspaceUrgent = 1 << 4,
        Name = 1 << 5,
        Output = 1 << 6,
        WorkspaceExtensions = 1 << 7,
        AllWorkspaceFields = (1 << 8) - 1
    };
    enum CastField : quint64 {
        CastActive = 1 << 0,
        DynamicTarget = 1 << 1,
        Kind = 1 << 2,
        CastPid = 1 << 3,
        NodeId = 1 << 4,
        SessionId = 1 << 5,
        Target = 1 << 6,
        CastExtensions = 1 << 7,
        AllCastFields = (1 << 8) - 1
    };
    struct Changes
    {
        quint64 epoch = 0, revision = 0;
        QMap<quint64, RecordChange<NiriWindow>> windows;
        QMap<quint64, RecordChange<NiriWorkspace>> workspaces;
        QMap<quint64, RecordChange<NiriCast>> casts;
        bool keyboardLayouts = false, overview = false, config = false, readiness = false;
        bool touchpad = false;
        bool reset = false;
        std::optional<quint64> focusedWindowBefore, focusedWorkspaceBefore;
        QString reason;
        bool changed() const;
    };
    struct ApplyResult
    {
        enum Status { Applied, StaleEpoch, ProtocolError } status = Applied;
        QString error;
        Changes changes;
    };

    ApplyResult apply(const NiriEvent &event, quint64 epoch);
    Changes reset(quint64 epoch, const QString &reason);
    QList<NiriEvent> replicate() const;
    quint64 epoch() const { return m_epoch; }
    quint64 revision() const { return m_revision; }
    bool initialStateReceived() const { return m_ready; }
    Entry<NiriWindow> window(quint64 id) const { return m_windows.value(id); }
    Entry<NiriWorkspace> workspace(quint64 id) const { return m_workspaces.value(id); }
    Entry<NiriCast> cast(quint64 streamId) const { return m_casts.value(streamId); }
    QList<NiriWindow> windowsSnapshot() const;
    QList<NiriWorkspace> workspacesSnapshot() const;
    QList<NiriCast> castsSnapshot() const;
    const std::optional<NiriKeyboardLayouts> &keyboardLayouts() const { return m_layouts; }
    bool overviewOpen() const { return m_overview; }
    bool configFailed() const { return m_config; }
    const std::optional<bool> &touchpadOn() const { return m_touchpad; }
    std::optional<quint64> focusedWindowId() const;
    std::optional<quint64> focusedWorkspaceId() const;
    Entry<NiriWorkspace> workspaceByName(const QString &name,
                                         const Changes *before = nullptr) const;
    QList<quint64> windowIds() const { return m_windows.keys(); }
    QList<quint64> workspaceIds() const { return m_workspaces.keys(); }
    QList<quint64> castIds() const { return m_casts.keys(); }
    int windowCount() const { return m_windows.size(); }
    int workspaceCount() const { return m_workspaces.size(); }
    int castCount() const { return m_casts.size(); }

private:
    QMap<quint64, Entry<NiriWindow>> m_windows;
    QMap<quint64, Entry<NiriWorkspace>> m_workspaces;
    QMap<quint64, Entry<NiriCast>> m_casts;
    std::optional<NiriKeyboardLayouts> m_layouts;
    std::optional<bool> m_touchpad;
    bool m_overview = false, m_config = false, m_ready = false;
    quint64 m_epoch = 0, m_revision = 0;
    QSet<quint64> m_focusedWindows, m_focusedWorkspaces;
    QMap<QString, QSet<quint64>> m_workspaceNames;
    void updateIndexes(const Changes &changes);
};
