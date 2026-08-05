# Control socket wire-shape inventory

Captured from the current implementation (`src/control_socket.cpp`,
`src/midipeer.cpp`, `src/local_alsa_listener.cpp`, `src/utils.cpp`,
`cli/rtpmidid-cli.py`). The typed redesign MUST reproduce these shapes
byte-compatibly; `tests/` byte-compat checks compare new serialization
against these payloads.

## Framing and envelopes

- Requests: newline-delimited JSON lines: `{"method": "<str>", "params": <any>, "id": "<str>"?}`.
  `id` is optional (the CLI omits it); unknown keys are tolerated.
- Responses: `{"id": <echo or null>, "result": <any>}` or `{"id": <echo or null>, "error": "<str>"}`.
  Newline-terminated; the trailing `\n` written only on success.
- Malformed request line → `{"error": "<parse message>"}` (no `id`).
- Unknown method → `{"id":..., "error": "Unknown method '<m>'"}`
- Unknown peer → `{"id":..., "error": "Unknown peer '<id>'"}`
- Peer command error → `{"id":..., "error": <peer error payload>}` (peer result
  containing an `error` key is hoisted to the top level).

## Global commands (params → result)

| command | params wire form | result wire form |
|---|---|---|
| `status` | ignored | object `{version, settings:{alsa_name, control_filename}, router:[peer_status], mdns:{status, announcements:[{name,port}], remote_announcements:[{name,hostname,port}]}}` |
| `router.remove` | **array** `[peer_id:int]` | string `"ok"` |
| `router.connect` | object `{from:int, to:int}` | string `"ok"` |
| `router.disconnect` | object `{from:int, to:int}` | string `"ok"` |
| `connect` | **4 forms**: `[hostname]`, `[hostname,port]`, `[name,hostname,port]` (all strings), or `{name,hostname,port}` — normalization: name defaults to hostname, port defaults `"5004"` | array `["ok"]` |
| `router.create` | object `{type:str, <per-type fields>}` | created peer's status object; or `{type:{field:desc,...}}` for `type:"list"`; or `{error:"Unknown peer type"}` |
| `mdns.remove` | object `{name:str, hostname:str\|null, port:int}` | string `"ok"` |
| `export.rawmidi` | object `{device:str (required), name?, local_udp_port?, remote_udp_port?, hostname?}` (missing optionals → `""`/`"0"` defaults) | array `["ok"]`; or `{error:"Need device", params:{device:desc,...}}` |
| `help` | ignored | array `[{name, description}, ...]` |

`router.create` per-type params:
`local_rawmidi_t`: `{name, device}`; `network_rtpmidi_client_t`: `{name, hostname, port}`;
`network_rtpmidi_listener_t`: `{name, udp_port}`; `local_alsa_peer_t`: `{name}`.

## Peer commands (`<peer_id>.<cmd>`)

Params and results are peer-defined; params arrive as arbitrary JSON.

- Default (`midipeer_t::command`): `status` → peer status; `help` → `{}`; else `{"error": "Command not implemented"}`.
- `local_alsa_listener_t` (type `"local_alsa_listener_t"`):
  - `add_endpoint`: params `{hostname:str, port:str|number}` → array `["ok"]`
  - `remove_endpoint`: params `{hostname:str, port:str|number}` → array `["ok"]` or `{"error": "Endpoint not found"}`
  - `help` → array `[{name, description}, ...]`

## Peer status shapes (router-enriched)

The router appends `id`, `send_to` (array of peer ids), `stats:{recv:int, sent:int}`,
`type` (get_type() string) AFTER the peer's own keys. Type strings:
`local_alsa_listener_t`, `local_alsa_peer_t`, `local_rawmidi_peer_t`,
`network_rtpmidi_client_t`, `network_rtpmidi_listener_t` (and the RTP peer
type). The `type` key's value comes from `get_type()` — preserved as-is.

- **network_rtpmidi_peer** (`utils.cpp::peer_status`):
  `{name:str, peer:{latency_ms:{last:float, average:float, stddev:float}, status:str, local:{sequence_number:int, sequence_number_ack:int, name:str, ssrc:int, port:int, hostname:str}, remote:{name:str, sequence_number:int, ssrc:int, port:int, hostname:str}}, id, send_to, stats, type}`
- **network_rtpmidi_client**: same RTP shape as `network_rtpmidi_peer`.
- **local_alsa_peer**: `{name:str, port:int, id, send_to, stats, type}`
- **local_rawmidi_peer**: `{name:str, device:str, status:str ("open"|"closed"), id, send_to, stats, type}`
- **local_alsa_listener**: `{name:str, endpoints:[{hostname:str, port:str}], connection_count:int, status:str ("CONNECTED"|"WAITING"), id, send_to, stats, type}` (note: endpoint `port` is a string)
- **network_rtpmidi_listener**: `{name:str, port:int, peers:[rtp_detail...], id, send_to, stats, type}`
- **network_rtpmidi_multi_listener**: `{peers:[rtp_detail...], name:str, listening:{name:str, control_port:int, midi_port:int}, id, send_to, stats, type}`
- **local_alsa_multi_listener**: `{name:str, connections:[{alsa:str, local:int}], id, send_to, stats, type}`

## Typed mapping plan (tasks 6.3–6.4)

- **Array-mode structs** (`/// [JSON-DM-ARRAY]`): `router.remove` params
  `{peer_id}` (1 element). Generic CLI positional params fall here too.
- **Hand-written reader** (public Reader API, boundary-local): `connect`
  (four forms + name/port normalization). No other command needs it.
- **Object-mode param structs**: `router.connect_params_t {from, to}`,
  `router.disconnect_params_t {from, to}`, `router.create_params_t`
  `{type, name?, device?, hostname?, port?, udp_port?}`, `mdns_remove_params_t`
  `{name, hostname: optional<string>, port}`, `export_rawmidi_params_t`
  `{device, name?, local_udp_port?, remote_udp_port?, hostname?}`
  (string optionals, empty-string semantics preserved by handler).
- **Result structs**: string `"ok"` → `ResultT = std::string`;
  array `["ok"]` → `ResultT = std::vector<std::string>` (faithful model);
  `status` → `daemon_status_t` struct; `router.create` → the created peer's
  status variant; `help` → `vector<command_help_t{name, description}>`.
- **Peer status** → `std::variant<rtp_peer_status_t, alsa_peer_status_t,
  rawmidi_peer_status_t, alsa_listener_status_t>`; each alternative carries
  the router members (`id`, `send_to`, `stats`, `type`) + its own exact keys
  (including RTP's `peer` subtree). Router assigns the router members via
  generic `std::visit`.

## Legacy quirks to reproduce (not "fix")

- `"ok"` vs `["ok"]` differ per command — reproduce each faithfully.
- `"id": null` echoes when the request omitted `id`.
- `connect` accepts four params forms.
- `mdns.remove` accepts `hostname: null`.
- `export.rawmidi`/`connect`/`mdns_status` may produce error-object results
  with a `params` help payload.
