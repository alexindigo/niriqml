use schemars::{generate::SchemaSettings, JsonSchema};
use serde_json::{json, Value};
use std::io::{self, Write};

#[cfg(not(feature = "newer"))]
mod vectors;
#[cfg(feature = "newer")]
#[path = "newer_vectors.rs"]
mod vectors;

#[cfg(not(feature = "newer"))]
const RELEASE: &str = "v26.04";
#[cfg(feature = "newer")]
const RELEASE: &str = "main-689c1e9";
#[cfg(not(feature = "newer"))]
const COMMIT: &str = "8ed0da44d974c32c6877d2f4630c314da0717ecb";
#[cfg(feature = "newer")]
const COMMIT: &str = "689c1e914a5af782a476e433b79c15554bf5a312";
#[cfg(not(feature = "newer"))]
const SCHEMARS_VERSION: &str = "1.2.1";
#[cfg(feature = "newer")]
const SCHEMARS_VERSION: &str = "1.2.2";
const EXTRACTION_FORMAT_VERSION: u32 = 1;
const USAGE: &str = "usage: niriqml-ipc-schema-dump [--contract deserialize|serialize | --vectors]";

// This is an extraction index, not a message in niri's wire protocol.
// The properties' actual refs identify the roots, including the Reply alias.
#[derive(JsonSchema)]
#[allow(dead_code)]
struct IpcRoots {
    request: niri_ipc::Request,
    reply: niri_ipc::Reply,
    response: niri_ipc::Response,
    event: niri_ipc::Event,
    action: niri_ipc::Action,
    output_action: niri_ipc::OutputAction,
}

#[derive(Clone, Copy, Debug, PartialEq)]
enum Contract {
    Deserialize,
    Serialize,
}

impl Contract {
    fn name(self) -> &'static str {
        match self {
            Self::Deserialize => "deserialize",
            Self::Serialize => "serialize",
        }
    }
}

fn parse_contract(args: impl IntoIterator<Item = String>) -> Result<Contract, &'static str> {
    let args: Vec<_> = args.into_iter().collect();
    match args.as_slice() {
        [] => Ok(Contract::Deserialize),
        [flag, value] if flag == "--contract" => match value.as_str() {
            "deserialize" => Ok(Contract::Deserialize),
            "serialize" => Ok(Contract::Serialize),
            _ => Err(USAGE),
        },
        _ => Err(USAGE),
    }
}

fn sort_keys(value: &mut Value) {
    match value {
        Value::Object(object) => {
            for child in object.values_mut() {
                sort_keys(child);
            }
            object.sort_keys();
        }
        Value::Array(array) => {
            for child in array {
                sort_keys(child);
            }
        }
        _ => {}
    }
}

fn schema(contract: Contract) -> Result<Value, serde_json::Error> {
    let settings = SchemaSettings::draft2020_12();
    let settings = match contract {
        Contract::Deserialize => settings.for_deserialize(),
        Contract::Serialize => settings.for_serialize(),
    };
    let mut schema =
        serde_json::to_value(settings.into_generator().into_root_schema_for::<IpcRoots>())?;
    schema["x-niri-source"] = json!({
        "release": RELEASE,
        "commit": COMMIT,
        "schemars_version": SCHEMARS_VERSION,
        "contract": contract.name(),
        "extraction_format_version": EXTRACTION_FORMAT_VERSION,
    });
    sort_keys(&mut schema);
    Ok(schema)
}

fn write_schema(contract: Contract) -> Result<(), Box<dyn std::error::Error>> {
    let stdout = io::stdout();
    let mut output = io::BufWriter::new(stdout.lock());
    serde_json::to_writer_pretty(&mut output, &schema(contract)?)?;
    writeln!(output)?;
    output.flush()?;
    Ok(())
}

fn main() {
    if std::env::args().skip(1).collect::<Vec<_>>() == ["--vectors"] {
        let result = vectors::write_vectors();
        if let Err(error) = result {
            eprintln!("vector serialization failed: {error}");
            std::process::exit(1);
        }
        return;
    }
    let contract = parse_contract(std::env::args().skip(1)).unwrap_or_else(|message| {
        eprintln!("{message}");
        std::process::exit(2);
    });
    if let Err(error) = write_schema(contract) {
        eprintln!("schema extraction failed: {error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn contracts_are_explicit_and_invalid_arguments_fail() {
        assert_eq!(parse_contract([]), Ok(Contract::Deserialize));
        for (name, expected) in [
            ("deserialize", Contract::Deserialize),
            ("serialize", Contract::Serialize),
        ] {
            assert_eq!(
                parse_contract(["--contract".into(), name.into()]),
                Ok(expected)
            );
        }
        for args in [
            vec!["--contract"],
            vec!["--contract", "invalid"],
            vec!["--unknown"],
            vec!["--contract", "serialize", "extra"],
        ] {
            assert!(parse_contract(args.into_iter().map(String::from)).is_err());
        }
    }

    #[test]
    fn sorts_objects_recursively_without_reordering_arrays() {
        let mut value = json!({"z": [{"z": 2, "a": 1}, 0], "a": {"z": 4, "a": 3}});
        sort_keys(&mut value);
        assert_eq!(
            serde_json::to_string(&value).unwrap(),
            r#"{"a":{"a":3,"z":4},"z":[{"a":1,"z":2},0]}"#
        );
    }

    #[test]
    fn schema_has_pinned_provenance_and_six_root_refs() {
        for contract in [Contract::Deserialize, Contract::Serialize] {
            let schema = schema(contract).unwrap();
            assert_eq!(
                schema["$schema"],
                "https://json-schema.org/draft/2020-12/schema"
            );
            assert_eq!(schema["x-niri-source"]["release"], RELEASE);
            assert_eq!(schema["x-niri-source"]["commit"], COMMIT);
            assert_eq!(
                schema["x-niri-source"]["schemars_version"],
                SCHEMARS_VERSION
            );
            assert_eq!(schema["x-niri-source"]["contract"], contract.name());
            assert_eq!(schema["x-niri-source"]["extraction_format_version"], 1);
            let roots = schema["properties"].as_object().unwrap();
            assert_eq!(roots.len(), 6);
            for root in roots.values() {
                let reference = root["$ref"].as_str().unwrap();
                assert!(schema
                    .pointer(reference.strip_prefix('#').unwrap())
                    .is_some());
            }
        }
    }
}
