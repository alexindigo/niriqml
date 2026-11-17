//! Independent oracle: construct released Rust types and serialize with upstream serde.
use niri_ipc::*;
use serde_json::{json, Value};
use std::io::{self, Write};

pub fn write_vectors() -> Result<(), Box<dyn std::error::Error>> {
    let mut vectors: Vec<Value> = Vec::new();
    macro_rules! sample {
        ($typ:ident, $name:literal, $value:expr) => {
            vectors.push(json!({"type": stringify!($typ), "name": $name,
                                "value": serde_json::to_value($value)?}));
        };
    }
    let timestamp = Timestamp {
        secs: u64::MAX,
        nanos: u32::MAX,
    };
    let layout = WindowLayout {
        pos_in_scrolling_layout: None,
        tile_size: (1.25, 0.0),
        window_size: (i32::MIN, i32::MAX),
        tile_pos_in_workspace_view: None,
        window_offset_in_tile: (-0.5, 12.0),
    };
    let present_layout = WindowLayout {
        pos_in_scrolling_layout: Some((9_007_199_254_740_993, usize::MAX)),
        tile_pos_in_workspace_view: Some((0.0, -0.5)),
        ..layout.clone()
    };
    let window = Window {
        id: u64::MAX,
        title: None,
        app_id: None,
        pid: None,
        workspace_id: None,
        is_focused: false,
        is_floating: true,
        is_urgent: false,
        layout: layout.clone(),
        focus_timestamp: None,
    };
    let logical = LogicalOutput {
        x: i32::MIN,
        y: i32::MAX,
        width: u32::MAX,
        height: 0,
        scale: 1.25,
        transform: Transform::_90,
    };
    let mode = Mode {
        width: u16::MAX,
        height: 0,
        refresh_rate: u32::MAX,
        is_preferred: false,
    };
    let output = Output {
        name: "DP-1".into(),
        make: "make".into(),
        model: "model".into(),
        serial: None,
        physical_size: None,
        modes: vec![mode],
        current_mode: None,
        is_custom_mode: false,
        vrr_supported: true,
        vrr_enabled: false,
        logical: None,
    };
    let workspace = Workspace {
        id: 9_007_199_254_740_993,
        idx: u8::MAX,
        name: None,
        output: None,
        is_urgent: true,
        is_active: false,
        is_focused: false,
        active_window_id: None,
    };
    let cast = Cast {
        stream_id: u64::MAX,
        session_id: 9_007_199_254_740_993,
        kind: CastKind::PipeWire,
        target: CastTarget::Nothing {},
        is_dynamic_target: false,
        is_active: true,
        pid: None,
        pw_node_id: None,
    };
    sample!(Overview, "overview", Overview { is_open: true });
    sample!(
        PickedColor,
        "picked-color",
        PickedColor {
            rgb: [0.0, 0.5, 1.0]
        }
    );
    sample!(
        ConfiguredMode,
        "configured-mode-null",
        ConfiguredMode {
            width: 0,
            height: u16::MAX,
            refresh: None
        }
    );
    sample!(
        ConfiguredMode,
        "configured-mode-present",
        ConfiguredMode {
            width: 1920,
            height: 1080,
            refresh: Some(0.0)
        }
    );
    sample!(
        ConfiguredPosition,
        "configured-position",
        ConfiguredPosition {
            x: i32::MIN,
            y: i32::MAX
        }
    );
    sample!(
        VrrToSet,
        "vrr-to-set",
        VrrToSet {
            vrr: false,
            on_demand: true
        }
    );
    sample!(Mode, "mode", mode);
    sample!(Timestamp, "timestamp", timestamp);
    sample!(WindowLayout, "window-layout-null", layout);
    sample!(WindowLayout, "window-layout-present", present_layout);
    sample!(Window, "window-null", window.clone());
    sample!(
        Window,
        "window-present",
        Window {
            id: 0,
            title: Some("".into()),
            app_id: Some("app\"\\\n".into()),
            pid: Some(i32::MIN),
            workspace_id: Some(u64::MAX),
            focus_timestamp: Some(timestamp),
            ..window
        }
    );
    sample!(LogicalOutput, "logical-output", logical);
    sample!(Output, "output-null", output.clone());
    sample!(
        Output,
        "output-present",
        Output {
            serial: Some("".into()),
            physical_size: Some((u32::MAX, 0)),
            current_mode: Some(usize::MAX),
            logical: Some(logical),
            ..output
        }
    );
    sample!(Workspace, "workspace-null", workspace.clone());
    sample!(
        Workspace,
        "workspace-present",
        Workspace {
            name: Some("".into()),
            output: Some("".into()),
            active_window_id: Some(0),
            ..workspace
        }
    );
    sample!(
        KeyboardLayouts,
        "keyboard-layouts",
        KeyboardLayouts {
            names: vec!["".into(), "English".into()],
            current_idx: u8::MAX
        }
    );
    sample!(
        LayerSurface,
        "layer-surface",
        LayerSurface {
            namespace: "namespace".into(),
            output: "DP-1".into(),
            layer: Layer::Overlay,
            keyboard_interactivity: LayerSurfaceKeyboardInteractivity::OnDemand,
        }
    );
    sample!(Cast, "cast-null", cast.clone());
    sample!(
        Cast,
        "cast-present",
        Cast {
            pid: Some(i32::MIN),
            pw_node_id: Some(u32::MAX),
            target: CastTarget::Window { id: u64::MAX },
            ..cast
        }
    );
    sample!(SizeChange, "size-fixed", SizeChange::SetFixed(i32::MIN));
    sample!(
        SizeChange,
        "size-proportion",
        SizeChange::SetProportion(1.25)
    );
    sample!(
        SizeChange,
        "size-adjust-fixed",
        SizeChange::AdjustFixed(i32::MAX)
    );
    sample!(
        SizeChange,
        "size-adjust-proportion",
        SizeChange::AdjustProportion(-0.5)
    );
    sample!(
        PositionChange,
        "position-fixed",
        PositionChange::SetFixed(0.0)
    );
    sample!(
        PositionChange,
        "position-proportion",
        PositionChange::SetProportion(1.25)
    );
    sample!(
        PositionChange,
        "position-adjust-fixed",
        PositionChange::AdjustFixed(-0.5)
    );
    sample!(
        PositionChange,
        "position-adjust-proportion",
        PositionChange::AdjustProportion(1.25)
    );
    sample!(
        WorkspaceReferenceArg,
        "workspace-id",
        WorkspaceReferenceArg::Id(u64::MAX)
    );
    sample!(
        WorkspaceReferenceArg,
        "workspace-index",
        WorkspaceReferenceArg::Index(u8::MAX)
    );
    sample!(
        WorkspaceReferenceArg,
        "workspace-name",
        WorkspaceReferenceArg::Name("".into())
    );
    sample!(LayoutSwitchTarget, "layout-next", LayoutSwitchTarget::Next);
    sample!(LayoutSwitchTarget, "layout-prev", LayoutSwitchTarget::Prev);
    sample!(
        LayoutSwitchTarget,
        "layout-index",
        LayoutSwitchTarget::Index(u8::MAX)
    );
    sample!(ColumnDisplay, "column-normal", ColumnDisplay::Normal);
    sample!(ColumnDisplay, "column-tabbed", ColumnDisplay::Tabbed);
    sample!(ModeToSet, "mode-auto", ModeToSet::Automatic);
    sample!(
        ModeToSet,
        "mode-specific",
        ModeToSet::Specific(ConfiguredMode {
            width: 0,
            height: 0,
            refresh: None
        })
    );
    sample!(HSyncPolarity, "hsync-positive", HSyncPolarity::PHSync);
    sample!(HSyncPolarity, "hsync-negative", HSyncPolarity::NHSync);
    sample!(VSyncPolarity, "vsync-positive", VSyncPolarity::PVSync);
    sample!(VSyncPolarity, "vsync-negative", VSyncPolarity::NVSync);
    sample!(ScaleToSet, "scale-auto", ScaleToSet::Automatic);
    sample!(ScaleToSet, "scale-specific", ScaleToSet::Specific(1.25));
    sample!(PositionToSet, "position-auto", PositionToSet::Automatic);
    sample!(
        PositionToSet,
        "position-specific",
        PositionToSet::Specific(ConfiguredPosition { x: -1, y: 0 })
    );
    sample!(Transform, "transform-normal", Transform::Normal);
    sample!(Transform, "transform-90", Transform::_90);
    sample!(Transform, "transform-180", Transform::_180);
    sample!(Transform, "transform-270", Transform::_270);
    sample!(Transform, "transform-flipped", Transform::Flipped);
    sample!(Transform, "transform-flipped-90", Transform::Flipped90);
    sample!(Transform, "transform-flipped-180", Transform::Flipped180);
    sample!(Transform, "transform-flipped-270", Transform::Flipped270);
    sample!(
        OutputConfigChanged,
        "config-applied",
        OutputConfigChanged::Applied
    );
    sample!(
        OutputConfigChanged,
        "config-missing",
        OutputConfigChanged::OutputWasMissing
    );
    sample!(Layer, "layer-background", Layer::Background);
    sample!(Layer, "layer-bottom", Layer::Bottom);
    sample!(Layer, "layer-top", Layer::Top);
    sample!(Layer, "layer-overlay", Layer::Overlay);
    sample!(
        LayerSurfaceKeyboardInteractivity,
        "interactivity-none",
        LayerSurfaceKeyboardInteractivity::None
    );
    sample!(
        LayerSurfaceKeyboardInteractivity,
        "interactivity-exclusive",
        LayerSurfaceKeyboardInteractivity::Exclusive
    );
    sample!(
        LayerSurfaceKeyboardInteractivity,
        "interactivity-demand",
        LayerSurfaceKeyboardInteractivity::OnDemand
    );
    sample!(CastKind, "cast-pipewire", CastKind::PipeWire);
    sample!(CastKind, "cast-screencopy", CastKind::WlrScreencopy);
    sample!(CastTarget, "target-nothing", CastTarget::Nothing {});
    sample!(
        CastTarget,
        "target-output",
        CastTarget::Output { name: "".into() }
    );
    sample!(
        CastTarget,
        "target-window",
        CastTarget::Window { id: u64::MAX }
    );
    let document =
        json!({"source": {"release": "v26.04", "commit": super::COMMIT}, "vectors": vectors});
    let stdout = io::stdout();
    let mut output = io::BufWriter::new(stdout.lock());
    serde_json::to_writer_pretty(&mut output, &document)?;
    writeln!(output)?;
    output.flush()?;
    Ok(())
}
