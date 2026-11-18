use niri_ipc::*;
use serde_json::{json, Value};
use std::io::{self, Write};

pub fn write_vectors() -> Result<(), Box<dyn std::error::Error>> {
    let mut vectors = Vec::<Value>::new();
    macro_rules! add {
        ($name:expr, $ty:expr, $value:expr) => {
            vectors.push(json!({"name": $name, "type": $ty, "value": $value}));
        };
    }
    for action in [
        Action::ChangeVt { vt: 2 },
        Action::ToggleTouchpad {},
        Action::SetWindowFullscreen { id: None },
        Action::UnsetWindowFullscreen { id: None },
        Action::SetWindowMaximizedToEdges { id: None },
        Action::UnsetWindowMaximizedToEdges { id: None },
        Action::SetWindowedFullscreen { id: None },
        Action::UnsetWindowedFullscreen { id: None },
        Action::SetWindowFullscreen { id: Some(u64::MAX) },
        Action::UnsetWindowFullscreen { id: Some(u64::MAX) },
        Action::SetWindowMaximizedToEdges { id: Some(u64::MAX) },
        Action::UnsetWindowMaximizedToEdges { id: Some(u64::MAX) },
        Action::SetWindowedFullscreen { id: Some(u64::MAX) },
        Action::UnsetWindowedFullscreen { id: Some(u64::MAX) },
    ] {
        let value = serde_json::to_value(&action)?;
        let tag = value.as_object().unwrap().keys().next().unwrap();
        let name = format!("Action.{tag}.{}", vectors.len());
        add!(name, "Action", action);
    }
    for max_bpc in [
        MaxBpc::_6,
        MaxBpc::_8,
        MaxBpc::_10,
        MaxBpc::_12,
        MaxBpc::_14,
        MaxBpc::_16,
    ] {
        let tag = serde_json::to_value(max_bpc)?.as_str().unwrap().to_owned();
        add!(format!("MaxBpc.{tag}"), "MaxBpc", max_bpc);
        add!(
            format!("OutputAction.MaxBpc.{tag}"),
            "OutputAction",
            OutputAction::MaxBpc { max_bpc }
        );
        add!(
            format!("Request.MaxBpc.{tag}"),
            "Request",
            Request::Output {
                output: "DP-1".into(),
                action: OutputAction::MaxBpc { max_bpc }
            }
        );
    }
    for kind in [
        CastKind::ExtImageCopyCapture,
        CastKind::ExtImageCopyCaptureCursor,
    ] {
        let tag = serde_json::to_value(kind)?.as_str().unwrap().to_owned();
        add!(format!("CastKind.{tag}"), "CastKind", kind);
    }
    for on in [false, true] {
        add!(
            format!("Event.TouchpadToggled.{on}"),
            "Event",
            Event::TouchpadToggled { on }
        );
    }
    for max_bpc in [None, Some(10), Some(0), Some(u8::MAX)] {
        add!(
            format!("Output.{max_bpc:?}"),
            "Output",
            Output {
                name: "DP-1".into(),
                make: "make".into(),
                model: "model".into(),
                serial: None,
                physical_size: None,
                modes: vec![],
                current_mode: None,
                is_custom_mode: false,
                vrr_supported: false,
                vrr_enabled: false,
                logical: None,
                max_bpc,
            }
        );
    }
    // Reference the newer defaults independently, without importing them into
    // the released contract used by niriqml.
    let mut defaults = Vec::new();
    for value in [
        json!({"Quit": {}}),
        json!({"Screenshot": {}}),
        json!({"MoveFloatingWindow": {}}),
    ] {
        let action: Action = serde_json::from_value(value)?;
        defaults.push(serde_json::to_value(action)?);
    }
    let document = json!({
        "source": {"release": super::RELEASE, "commit": super::COMMIT},
        "vectors": vectors, "newerDefaultExamples": defaults,
    });
    let mut output = io::BufWriter::new(io::stdout().lock());
    serde_json::to_writer_pretty(&mut output, &document)?;
    writeln!(output)?;
    output.flush()?;
    Ok(())
}
