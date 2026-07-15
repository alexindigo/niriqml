# niriqml

Typed QML bindings for [niri](https://github.com/YaLTeR/niri) IPC — windows,
workspaces, events, actions — as a Qt 6 QML plugin.

```
import Niri 1.0

Repeater {
    model: NiriWindows
    delegate: Rectangle {
        color: model.window.isFocused ? "orange" : "gray"
        Text { text: model.window.title }
    }
}
```

No JSON parsing in QML, no external daemon, no polling — every niri property
arrives as a `Q_PROPERTY`, every niri event as a Qt signal, every niri action
as a `Q_INVOKABLE`.

See [DESIGN.md](DESIGN.md) for the full architecture.
