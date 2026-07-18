# Changelog

## 2026-07-18

### Feature

Initial public release of niriqml — typed, reactive QML bindings for the niri
Wayland compositor IPC. The library provides a full QML-native API surface:
zero JSON parsing in QML, no polling, auto-reconnect with cached snapshots for
late subscribers. Tested against niri 26.04.

- Wire protocol with line-buffered JSON framing and exponential-backoff
  auto-reconnect, QML singleton with CppOwnership (`cfe945a`)
- Full niri 26.04 gadget schema: 9 value types (NiriWindow, NiriWorkspace,
  NiriOutput, NiriKeyboardLayouts, etc.) with QML_VALUE_TYPE registration and
  operator== for Q_PROPERTY change detection (`a7b47f1`)
- Dynamic JSON-to-gadget converter using Q_PROPERTY reflection, auto
  camelCase↔snake_case, null-safe and extra-key-safe (`a35b26d`)
- Typed event dispatch: 14 signals (one per niri event) plus rawEvent
  catchall; cached snapshots bootstrap late subscribers with consistent
  isFocused state (`374abbd`)
- Layer 2 QML primitives: NiriWindowsModel / NiriWorkspacesModel (QAbstractListModel
  with 9 roles), NiriState (7 derived globals), NiriWindowReactive /
  NiriWorkspaceReactive (per-id/name tracking with cache bootstrap) (`f248941`)
- Write path: 9 typed actions (focus, close, spawn, move, resize, float) with
  verified niri 26.04 wire formats; 7 typed queries (windows, workspaces,
  outputs, focused, keyboard layouts, version); sendRaw escape hatch for
  forward-compat (`b70ceae`)
- Observability dashboard (DashboardVisualizer.qml) with run-test-dash
  launcher (`ea63dd6`)

### Fix

- Add `operator!=` to all 8 Q_GADGETs in niritypes.h. Qt 6.5 MOC generates
  `operator!=` for Q_PROPERTY change detection but C++17 does not
  auto-synthesize `!=` from `==` (that's C++20). Qt ≥ 6.11 does not trigger
  this; aqtinstall Qt 6.5 does. (`3698eef`)
- Pin `Cpp11BracedListStyle: true` in `.clang-format`. The WebKit base style
  default for this key differs between clang-format 20 (Ubuntu 24.04) and 22
  (Arch). Adds 8 mock-based headless tests for NiriEvents typed dispatch
  (`1f13dd4`)
- Iterate CI configuration: Qt 6.10 (ICU 74 compatibility), clang-format-20,
  system deps (libgl, libdbus, libxkbcommon), coverage on/off (`f792fe7`
  through `b28a8d1`)

### Refactor

- Adopt canonical Qt `.clang-format` (from qt/qt5.git supermodule) and reformat
  all 20 C++ files. Enables CI lint enforcement going forward (`bd3c9ab`)

### Docs

- Comprehensive README.md with verified-by-inspection QML snippets covering
  every API surface (models, state, events, reactives, actions, queries,
  sendRaw) (`4e330e2`)
- Committed niri 26.04 IPC schema snapshot (`docs/SCHEMA.md`)
- FutureDevelopment.md with executable-README-snippets idea, remaining IPC
  surface, packaging, and performance backlog items

### Test

- QML registration smoke test: verifies all 5 singletons + 5 QML_NAMED_ELEMENT
  types are creatable and key Q_PROPERTYs exist. Runs headless (no
  NIRI_SOCKET), compatible with CI (`8cedfd1`)
- CHANGELOG.md with commit-date-grouped severity blocks (`f8b17eb`)
