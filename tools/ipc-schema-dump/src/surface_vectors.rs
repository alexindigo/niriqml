//! Callable/event oracle. No schema or generated C++ participates in construction.
use niri_ipc::*;
use serde_json::{json, Value};

pub fn extend(
    vectors: &mut Vec<Value>,
    window: &Window,
    output: &Output,
    workspace: &Workspace,
    layout: &WindowLayout,
    cast: &Cast,
    timestamp: Timestamp,
) -> Result<(), serde_json::Error> {
    macro_rules! sample {
        ($typ:ident, $name:expr, $value:expr) => {
            vectors.push(json!({"type": stringify!($typ), "name": $name,
                                "value": serde_json::to_value($value)?}));
        };
    }
    macro_rules! empty_actions {
        ($($tag:ident),+ $(,)?) => {$(
            sample!(Action, concat!("action-", stringify!($tag)), Action::$tag {});
        )+};
    }
    empty_actions!(
        PowerOffMonitors,
        PowerOnMonitors,
        ToggleKeyboardShortcutsInhibit,
        FocusWindowPrevious,
        FocusColumnLeft,
        FocusColumnRight,
        FocusColumnFirst,
        FocusColumnLast,
        FocusColumnRightOrFirst,
        FocusColumnLeftOrLast,
        FocusWindowOrMonitorUp,
        FocusWindowOrMonitorDown,
        FocusColumnOrMonitorLeft,
        FocusColumnOrMonitorRight,
        FocusWindowDown,
        FocusWindowUp,
        FocusWindowDownOrColumnLeft,
        FocusWindowDownOrColumnRight,
        FocusWindowUpOrColumnLeft,
        FocusWindowUpOrColumnRight,
        FocusWindowOrWorkspaceDown,
        FocusWindowOrWorkspaceUp,
        FocusWindowTop,
        FocusWindowBottom,
        FocusWindowDownOrTop,
        FocusWindowUpOrBottom,
        MoveColumnLeft,
        MoveColumnRight,
        MoveColumnToFirst,
        MoveColumnToLast,
        MoveColumnLeftOrToMonitorLeft,
        MoveColumnRightOrToMonitorRight,
        MoveWindowDown,
        MoveWindowUp,
        MoveWindowDownOrToWorkspaceDown,
        MoveWindowUpOrToWorkspaceUp,
        ConsumeWindowIntoColumn,
        ExpelWindowFromColumn,
        SwapWindowRight,
        SwapWindowLeft,
        ToggleColumnTabbedDisplay,
        CenterColumn,
        CenterVisibleColumns,
        FocusWorkspaceDown,
        FocusWorkspaceUp,
        FocusWorkspacePrevious,
        MoveWorkspaceDown,
        MoveWorkspaceUp,
        FocusMonitorLeft,
        FocusMonitorRight,
        FocusMonitorDown,
        FocusMonitorUp,
        FocusMonitorPrevious,
        FocusMonitorNext,
        MoveWindowToMonitorLeft,
        MoveWindowToMonitorRight,
        MoveWindowToMonitorDown,
        MoveWindowToMonitorUp,
        MoveWindowToMonitorPrevious,
        MoveWindowToMonitorNext,
        MoveColumnToMonitorLeft,
        MoveColumnToMonitorRight,
        MoveColumnToMonitorDown,
        MoveColumnToMonitorUp,
        MoveColumnToMonitorPrevious,
        MoveColumnToMonitorNext,
        SwitchPresetColumnWidth,
        SwitchPresetColumnWidthBack,
        MaximizeColumn,
        ExpandColumnToAvailableWidth,
        ShowHotkeyOverlay,
        MoveWorkspaceToMonitorLeft,
        MoveWorkspaceToMonitorRight,
        MoveWorkspaceToMonitorDown,
        MoveWorkspaceToMonitorUp,
        MoveWorkspaceToMonitorPrevious,
        MoveWorkspaceToMonitorNext,
        ToggleDebugTint,
        DebugToggleOpaqueRegions,
        DebugToggleDamage,
        FocusFloating,
        FocusTiling,
        SwitchFocusBetweenFloatingAndTiling,
        ClearDynamicCastTarget,
        ToggleOverview,
        OpenOverview,
        CloseOverview,
    );
    macro_rules! optional_id_actions {
        ($($tag:ident),+ $(,)?) => {$(
            sample!(Action, concat!("action-", stringify!($tag)), Action::$tag { id: Some(u64::MAX) });
            sample!(Action, concat!("action-", stringify!($tag), "-null"), Action::$tag { id: None });
        )+};
    }
    optional_id_actions!(
        CloseWindow,
        FullscreenWindow,
        ToggleWindowedFullscreen,
        ConsumeOrExpelWindowLeft,
        ConsumeOrExpelWindowRight,
        CenterWindow,
        ResetWindowHeight,
        SwitchPresetWindowWidth,
        SwitchPresetWindowWidthBack,
        SwitchPresetWindowHeight,
        SwitchPresetWindowHeightBack,
        MaximizeWindowToEdges,
        ToggleWindowFloating,
        MoveWindowToFloating,
        MoveWindowToTiling,
        ToggleWindowRuleOpacity,
        SetDynamicCastWindow,
    );
    macro_rules! required_id_actions {
        ($($tag:ident),+ $(,)?) => {$(
            sample!(Action, concat!("action-", stringify!($tag)), Action::$tag { id: u64::MAX });
        )+};
    }
    required_id_actions!(
        FocusWindow,
        ToggleWindowUrgent,
        SetWindowUrgent,
        UnsetWindowUrgent
    );
    macro_rules! focus_actions {
        ($($tag:ident),+ $(,)?) => {$(
            sample!(Action, concat!("action-", stringify!($tag)), Action::$tag { focus: false });
        )+};
    }
    focus_actions!(
        MoveWindowToWorkspaceDown,
        MoveWindowToWorkspaceUp,
        MoveColumnToWorkspaceDown,
        MoveColumnToWorkspaceUp
    );
    macro_rules! action {
        ($tag:ident { $($body:tt)* }) => {
            sample!(Action, concat!("action-", stringify!($tag)), Action::$tag { $($body)* });
        };
    }
    action!(Quit {
        skip_confirmation: false
    });
    action!(Spawn {
        command: vec!["printf".into(), "quote\"\\\n".into()]
    });
    action!(SpawnSh {
        command: "printf 'hello'\n".into()
    });
    action!(DoScreenTransition {
        delay_ms: Some(u16::MAX)
    });
    action!(Screenshot {
        show_pointer: false,
        path: Some("/screenshots/quote\"\\\n.png".into())
    });
    action!(ScreenshotScreen {
        write_to_disk: false,
        show_pointer: true,
        path: None
    });
    action!(ScreenshotWindow {
        id: Some(u64::MAX),
        write_to_disk: true,
        show_pointer: false,
        path: None
    });
    action!(FocusWindowInColumn { index: u8::MAX });
    action!(FocusColumn { index: usize::MAX });
    action!(MoveColumnToIndex { index: usize::MAX });
    action!(SetColumnDisplay {
        display: ColumnDisplay::Tabbed
    });
    action!(FocusWorkspace {
        reference: WorkspaceReferenceArg::Id(u64::MAX)
    });
    // Explicit regression fixture requested by the full-API plan.
    action!(MoveWindowToWorkspace {
        window_id: Some(123),
        reference: WorkspaceReferenceArg::Id(456),
        focus: false
    });
    action!(MoveColumnToWorkspace {
        reference: WorkspaceReferenceArg::Name("chat".into()),
        focus: false
    });
    action!(MoveWorkspaceToIndex {
        index: usize::MAX,
        reference: Some(WorkspaceReferenceArg::Id(u64::MAX))
    });
    action!(SetWorkspaceName {
        name: "quote\"\\\n".into(),
        workspace: Some(WorkspaceReferenceArg::Index(u8::MAX))
    });
    action!(UnsetWorkspaceName {
        reference: Some(WorkspaceReferenceArg::Name("chat".into()))
    });
    action!(FocusMonitor {
        output: "DP-1".into()
    });
    action!(MoveWindowToMonitor {
        id: Some(u64::MAX),
        output: "DP-1".into()
    });
    action!(MoveColumnToMonitor {
        output: "DP-1".into()
    });
    action!(SetWindowWidth {
        id: Some(u64::MAX),
        change: SizeChange::SetFixed(i32::MAX)
    });
    action!(SetWindowHeight {
        id: Some(u64::MAX),
        change: SizeChange::AdjustProportion(-12.5)
    });
    action!(SetColumnWidth {
        change: SizeChange::SetProportion(33.25)
    });
    action!(SwitchLayout {
        layout: LayoutSwitchTarget::Index(u8::MAX)
    });
    action!(MoveWorkspaceToMonitor {
        output: "DP-1".into(),
        reference: Some(WorkspaceReferenceArg::Id(u64::MAX))
    });
    action!(MoveFloatingWindow {
        id: Some(u64::MAX),
        x: PositionChange::AdjustFixed(-12.5),
        y: PositionChange::SetProportion(0.0)
    });
    action!(SetDynamicCastMonitor {
        output: Some("DP-1".into())
    });
    action!(StopCast {
        session_id: u64::MAX
    });
    action!(LoadConfigFile {
        path: Some("/config/quote\"\\\n.kdl".into())
    });

    // Every optional action field has independently serialized None and Some.
    sample!(
        Action,
        "action-DoScreenTransition-null",
        Action::DoScreenTransition { delay_ms: None }
    );
    sample!(
        Action,
        "action-Screenshot-null",
        Action::Screenshot {
            show_pointer: true,
            path: None
        }
    );
    sample!(
        Action,
        "action-ScreenshotScreen-present",
        Action::ScreenshotScreen {
            write_to_disk: true,
            show_pointer: false,
            path: Some("".into())
        }
    );
    sample!(
        Action,
        "action-ScreenshotWindow-null",
        Action::ScreenshotWindow {
            id: None,
            write_to_disk: false,
            show_pointer: true,
            path: Some("".into())
        }
    );
    sample!(
        Action,
        "action-MoveWindowToWorkspace-null",
        Action::MoveWindowToWorkspace {
            window_id: None,
            reference: WorkspaceReferenceArg::Id(u64::MAX),
            focus: false
        }
    );
    sample!(
        Action,
        "action-MoveWorkspaceToIndex-null",
        Action::MoveWorkspaceToIndex {
            index: 0,
            reference: None
        }
    );
    sample!(
        Action,
        "action-SetWorkspaceName-null",
        Action::SetWorkspaceName {
            name: "".into(),
            workspace: None
        }
    );
    sample!(
        Action,
        "action-UnsetWorkspaceName-null",
        Action::UnsetWorkspaceName { reference: None }
    );
    sample!(
        Action,
        "action-MoveWindowToMonitor-null",
        Action::MoveWindowToMonitor {
            id: None,
            output: "DP-1".into()
        }
    );
    sample!(
        Action,
        "action-MoveWorkspaceToMonitor-null",
        Action::MoveWorkspaceToMonitor {
            output: "DP-1".into(),
            reference: None
        }
    );
    sample!(
        Action,
        "action-MoveFloatingWindow-null",
        Action::MoveFloatingWindow {
            id: None,
            x: PositionChange::SetFixed(0.0),
            y: PositionChange::AdjustProportion(-1.25)
        }
    );
    sample!(
        Action,
        "action-SetDynamicCastMonitor-null",
        Action::SetDynamicCastMonitor { output: None }
    );
    sample!(
        Action,
        "action-LoadConfigFile-null",
        Action::LoadConfigFile { path: None }
    );
    for (name, change) in [
        ("fixed", SizeChange::SetFixed(i32::MIN)),
        ("proportion", SizeChange::SetProportion(33.25)),
        ("adjust-fixed", SizeChange::AdjustFixed(-10)),
        ("adjust-proportion", SizeChange::AdjustProportion(-12.5)),
    ] {
        sample!(
            Action,
            format!("action-width-{name}"),
            Action::SetWindowWidth { id: None, change }
        );
        sample!(
            Action,
            format!("action-height-{name}"),
            Action::SetWindowHeight { id: None, change }
        );
    }

    let output_actions = [
        OutputAction::Off,
        OutputAction::On,
        OutputAction::Mode {
            mode: ModeToSet::Automatic,
        },
        OutputAction::CustomMode {
            mode: ConfiguredMode {
                width: 1920,
                height: 1080,
                refresh: Some(60.0),
            },
        },
        OutputAction::Modeline {
            clock: 148.5,
            hdisplay: 1920,
            hsync_start: 2008,
            hsync_end: 2052,
            htotal: 2200,
            vdisplay: 1080,
            vsync_start: 1084,
            vsync_end: 1089,
            vtotal: 1125,
            hsync_polarity: HSyncPolarity::PHSync,
            vsync_polarity: VSyncPolarity::NVSync,
        },
        OutputAction::Scale {
            scale: ScaleToSet::Specific(1.25),
        },
        OutputAction::Transform {
            transform: Transform::_270,
        },
        OutputAction::Position {
            position: PositionToSet::Specific(ConfiguredPosition { x: -1920, y: 0 }),
        },
        OutputAction::Vrr {
            vrr: VrrToSet {
                vrr: true,
                on_demand: false,
            },
        },
    ];
    for (index, action) in output_actions.into_iter().enumerate() {
        sample!(
            OutputAction,
            format!("output-action-{index}"),
            action.clone()
        );
        sample!(
            Request,
            format!("request-output-{index}"),
            Request::Output {
                output: "DP-1".into(),
                action
            }
        );
    }
    macro_rules! unit_requests {
        ($($tag:ident),+ $(,)?) => {$(
            sample!(Request, concat!("request-", stringify!($tag)), Request::$tag);
        )+};
    }
    unit_requests!(
        Version,
        Outputs,
        Workspaces,
        Windows,
        Layers,
        KeyboardLayouts,
        FocusedOutput,
        FocusedWindow,
        PickWindow,
        PickColor,
        EventStream,
        ReturnError,
        OverviewState,
        Casts
    );
    sample!(
        Request,
        "request-action",
        Request::Action(Action::MoveWindowToWorkspace {
            window_id: Some(123),
            reference: WorkspaceReferenceArg::Id(456),
            focus: false
        })
    );

    let layouts = KeyboardLayouts {
        names: vec!["us".into(), "de".into()],
        current_idx: 1,
    };
    let responses = [
        Response::Handled,
        Response::Version("26.04\nquote\"\\".into()),
        Response::Outputs(std::collections::HashMap::from([(
            "DP-1".into(),
            output.clone(),
        )])),
        Response::Workspaces(vec![workspace.clone()]),
        Response::Windows(vec![window.clone()]),
        Response::Layers(vec![LayerSurface {
            namespace: "panel".into(),
            output: "DP-1".into(),
            layer: Layer::Top,
            keyboard_interactivity: LayerSurfaceKeyboardInteractivity::OnDemand,
        }]),
        Response::KeyboardLayouts(layouts.clone()),
        Response::FocusedOutput(Some(output.clone())),
        Response::FocusedWindow(Some(window.clone())),
        Response::PickedWindow(Some(window.clone())),
        Response::PickedColor(Some(PickedColor {
            rgb: [0.0, 0.5, 1.0],
        })),
        Response::OutputConfigChanged(OutputConfigChanged::Applied),
        Response::OverviewState(Overview { is_open: true }),
        Response::Casts(vec![cast.clone()]),
    ];
    for (index, response) in responses.into_iter().enumerate() {
        sample!(Response, format!("response-{index}"), response);
    }
    sample!(
        Response,
        "response-focused-output-null",
        Response::FocusedOutput(None)
    );
    sample!(
        Response,
        "response-focused-window-null",
        Response::FocusedWindow(None)
    );
    sample!(
        Response,
        "response-picked-window-null",
        Response::PickedWindow(None)
    );
    sample!(
        Response,
        "response-picked-color-null",
        Response::PickedColor(None)
    );
    sample!(
        Response,
        "response-output-missing",
        Response::OutputConfigChanged(OutputConfigChanged::OutputWasMissing)
    );

    macro_rules! event {
        ($tag:ident { $($body:tt)* }) => {
            sample!(Event, concat!("event-", stringify!($tag)), Event::$tag { $($body)* });
        };
    }
    event!(WorkspacesChanged {
        workspaces: vec![workspace.clone()]
    });
    event!(WorkspaceUrgencyChanged {
        id: u64::MAX,
        urgent: true
    });
    event!(WorkspaceActivated {
        id: u64::MAX,
        focused: false
    });
    event!(WorkspaceActiveWindowChanged {
        workspace_id: u64::MAX,
        active_window_id: Some(0)
    });
    event!(WindowsChanged {
        windows: vec![window.clone()]
    });
    event!(WindowOpenedOrChanged {
        window: window.clone()
    });
    event!(WindowClosed { id: u64::MAX });
    event!(WindowFocusChanged { id: Some(u64::MAX) });
    event!(WindowFocusTimestampChanged {
        id: u64::MAX,
        focus_timestamp: Some(timestamp)
    });
    event!(WindowUrgencyChanged {
        id: u64::MAX,
        urgent: false
    });
    event!(WindowLayoutsChanged {
        changes: vec![(u64::MAX, layout.clone())]
    });
    event!(KeyboardLayoutsChanged {
        keyboard_layouts: layouts
    });
    event!(KeyboardLayoutSwitched { idx: u8::MAX });
    event!(OverviewOpenedOrClosed { is_open: true });
    event!(ConfigLoaded { failed: false });
    event!(ScreenshotCaptured {
        path: Some("quote\"\\\n.png".into())
    });
    event!(CastsChanged {
        casts: vec![cast.clone()]
    });
    event!(CastStartedOrChanged { cast: cast.clone() });
    event!(CastStopped {
        stream_id: u64::MAX
    });
    sample!(
        Event,
        "event-workspace-window-null",
        Event::WorkspaceActiveWindowChanged {
            workspace_id: u64::MAX,
            active_window_id: None
        }
    );
    sample!(
        Event,
        "event-window-focus-null",
        Event::WindowFocusChanged { id: None }
    );
    sample!(
        Event,
        "event-window-timestamp-null",
        Event::WindowFocusTimestampChanged {
            id: u64::MAX,
            focus_timestamp: None
        }
    );
    sample!(
        Event,
        "event-screenshot-null",
        Event::ScreenshotCaptured { path: None }
    );
    Ok(())
}
