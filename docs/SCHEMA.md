# Niri IPC Schema Snapshot

Target niri version: _TBD_

## Protocol

- Transport: Unix socket at `$NIRI_SOCKET`
- Framing: JSON, one line per message
- Request format: `{ "Request": { "VariantName": { ... } } }`
- Reply format: `{ "Ok": { "ResponseVariant": { ... } } }` or `{ "Err": "..." }`

## Events

_TBD — populated from `niri-ipc` crate source_
