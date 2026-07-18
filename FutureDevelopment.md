# Future Development

## Executable README snippets

Adopt the approach from [reamde](https://github.com/alexindigo/reamde): extract
code fences from README.md and run them as tests. For niriqml this would mean:

- QML snippets in README become `qmltestrunner` test cases
- Headless-viable snippets (model instantiation, property checks) run in CI
- Live-dependent snippets (actions, queries) tagged and skipped without `NIRI_SOCKET`

This ensures documentation stays accurate — every breaking API change fails the
README-snippet test.

## Remaining niri IPC surface

- **Window-rules** — niri's `window-rule` IPC is not yet bound
- **Keybinds** — programmatic keybind management via IPC
- **Touch gestures** — gesture definition and query
- **Input** — keyboard/mouse state queries

## Packaging

- **AUR** — `niriqml-git` package
- **vcpkg / conan** — for non-Arch workflows
- **CI pre-built artifacts** — built libraries downloadable from GitHub releases

## Performance

- **Lazy event parsing** — currently every event is deserialized into a QVariantMap
  eagerly. Batch events or defer parsing when no subscriber exists.
- **Model diff** — `NiriWindowsModel` and `NiriWorkspacesModel` rebuild the full
  internal map on every change. Switch to incremental row insert/remove for
  large sessions.

## Tooling

- **`clang-format` enforcement** — the `.clang-format` is Qt canonical, but
  format-on-save is not yet enabled in the Zed project settings. Add
  `.zed/settings.json` with `format_on_save: "on"` for C++.
