# Future development

The checked [FeatureMapping](FeatureMapping.md) and [generated API](docs/generated/IPC-API.md)
define current coverage. Typed/raw access and retained stream state are distinct.
Do not infer window-rule/keybind/gesture configuration IPC that niri's released
schema does not provide.

| # | Area | Remaining investigation |
|---|---|---|
| 1 | Compatibility | Exercise declared Qt 6.5 build/QML/install at runtime; current guest uses Qt 6.12 |
| 2 | Teardown | Historical coverage/QML crash remains unconfirmed after bounded non-reproduction; retain honest toolchain-specific evidence |
| 3 | Compositor capabilities | Atomic arbitrary-window insertion, stable column identity and output hotplug events need upstream IPC, not missing wrappers |
| 4 | Packaging | Additional vcpkg/Conan integrations can consume the exported CMake target; AUR/source installation already exists |
| 5 | Performance | Profile actual large sessions before changing the exact parser, sparse reducer or ID-diff projections; they no longer rebuild competing maps per event |

Executable documentation/example checks, committed generated references, the
owned nested-niri live harness and target-based installation are implemented,
not roadmap placeholders. Release schema/behavior upgrades follow
[DESIGN.md](DESIGN.md#per-release-maintenance-procedure).
