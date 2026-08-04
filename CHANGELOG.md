# Changelog

## 2026-08-03

### Fix

- WindowFocusChanged with focal-window=null (id:0) now correctly clears
  focused state. niri sends `{id: None}` when switching to an empty
  workspace; the old code silently dropped `id==0`, keeping the last
  focused window's state stale forever (`11ce8b0`, `b219c86`)
- NiriConnection shutdown SIGSEGV: the implicit destructor called
  `~QLocalSocket() → disconnectFromHost()` which started a QTimer while
  Qt's event dispatcher was half-gone. Explicit destructor now calls
  `abort()` (immediate close) and stops the reconnect timer first
  (`4bc7b95`)
- NiriRequests timeout timer upgraded from `killTimer()` to `QTimer*`
  with context-bound connection. When a reply arrives before the
  timeout fires, `deleteLater` on the timer severs the connection
  automatically — prevents use-after-free from a late/spurious timeout
  lambda dereferencing freed `PendingRequest` (`d41eb31`)

All three found and validated in Atmosphera integration testing.

## 2026-08-02

### Fix

- Stack use-after-free in `NiriWorkspaceReactive::onWorkspaceActivated`:
  the `target` pointer took the address of a stack-local `w` in the for-loop
  body. After loop exit, `w` was destroyed and `target->output` dereferenced
  dangling memory. Replaced with `std::optional<NiriWorkspace>` (`2a892eb`,
  `97377f6`)
- Heap accumulation of `NiriPendingReply` objects: replies were parented to
  long-lived singletons (NiriActions, NiriRequests) and retained indefinitely.
  Added `connect(finished, deleteLater)` in the constructor (`1067224`,
  `5eee509`)
- Missing timeout on query sockets: if niri hangs, `NiriRequests::sendJson()`
  left the `PendingRequest` and `QLocalSocket` in memory indefinitely. Added
  configurable timeout (default 5s) with `QTimer` per request (`9a0fc0a`,
  `c363001`)
- Repeated `QByteArray` allocations in `jsonToGadget()`:
  `toSnakeCase(prop.name())` called on every property of every gadget
  conversion. Cached per `QMetaObject` in a static hash (`2fa9605`,
  `de5c0a6`)

All four findings from Gemini code review of v0.1.0. Each fix includes a
regression test verified to fail on pre-fix code.

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
  auto-synthesize `!=` from `==` (that's C++20). Qt >=6.11 does not trigger
  this; aqtinstall Qt 6.5 does. (`b3e4f10`)
- Pin `Cpp11BracedListStyle: true` in `.clang-format`. The WebKit base style
  default for this key differs between clang-format 20 (Ubuntu 24.04) and 22
  (Arch). Without the pin, both versions emit conflicting formatting. (`39ece54`)

### CI

- GitHub Actions workflow on ubuntu-24.04: Qt 6.10 via install-qt-action
  (ICU 74 compatible), cmake+ninja debug build with coverage, ctest excluding
  live_* and test_api targets, lcov capture and HTML artifact upload. Lint
  job runs clang-format-20 dry-run --Werror on all C++ sources. (`4f9a89e`)

### Refactor

- Adopt canonical Qt `.clang-format` (from qt/qt5.git supermodule) and reformat
  all 20 C++ files. Enables CI lint enforcement going forward (`bd3c9ab`)

### Docs

- Comprehensive README.md with verified-by-inspection QML snippets covering
  every API surface (models, state, events, reactives, actions, queries,
  sendRaw) (`4e330e2`)
- API.md: full reference covering 5 singletons, 5 elements, 8 value types,
  NiriPendingReply, NiriError. Every Q_PROPERTY/signal/Q_INVOKABLE spelled out
- Committed niri 26.04 IPC schema snapshot (`docs/SCHEMA.md`)
- FutureDevelopment.md with executable-README-snippets idea, remaining IPC
  surface, packaging, and performance backlog items

### Test

- QML registration smoke test: verifies all 5 singletons + 5 QML_NAMED_ELEMENT
  types are creatable and key Q_PROPERTYs exist. Runs headless (no
  NIRI_SOCKET), compatible with CI (`8cedfd1`)
- 8 mock-based headless tests for NiriEvents typed dispatch (test_events).
  Injects synthetic events via QMetaObject::invokeMethod and asserts typed
  signals and cached snapshots (`39ece54`)
