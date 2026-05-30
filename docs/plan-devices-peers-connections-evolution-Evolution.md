# MIDI Devices and Peers

Currently I have a big confussion about peers, devices, connections
local devices, remote devices. I'll let you know how I see it, I want 
you to tell whats wrong on my model and where it breaks. 

Devices:
 * Any MIDI capable gadget (local or remote) capable or receiving midi data. 
 * Can be local or remote.
 * Local include: ALSA sequencer ports, rawmidi devices.
 * Remote include: RTP-MIDI servers
 * They can have a stable identifier, so ephemeral ones are not needed as IP, 
   port, alsa sequencer port, rawmidi device file... are not needed. Stable ones 
   do not change.
 * Stable ids are in the form of <type>:<properties>, where type is a short string, examples:
    * alsa_seq:name=Peak,port=In
    * rawmidi:device=/dev/snd/midiC4D0,name=MIDI Export
    * rawmidi:name=MIDI Export
    * rtpmidi_client:hostname=host.local,service=Peak In
    * rtpmidi_client:hostname=host.local,service=Peak Out
    * rtpmidi_client:hostname=host.local,service=Peak Out,port=4001
    * rtpmidi_client:ip=192.168.1.100,port=4001
    * rtpmidi_server:name=Peak InOut,port=5004
    * rtpmidi_server:name=Hydrasynth

 * Stable Ids define many parameters, but when used as a reminder for known devices, maybe
   only some parameters are desired, for exampe to allow Peak to be on diferent hostnames or 
   ips. So parameters are optional, and can be omitted.

> 🔎 **Current code:** There is *no* `Device` abstraction yet — every endpoint is a
> `midipeer_t` (`src/midipeer.hpp`). A stable-id concept *does* exist in
> `compute_stable_id()` (`src/connection_db.cpp:118`) but it uses a positional
> `type:part:part` form with `:`→`|` escaping (e.g. `rtpmidi:host.local:Peak Out`,
> `rawmidi:/dev/snd/midiC4D0`, `rtpmidi_server:Peak InOut`), **not** `key=value`.
> "Optional/omitted parameters" are **not** supported: `make_stable_id()`
> (`connection_db.cpp:46`) returns `nullopt` if any part is empty and matching is exact
> string equality (`find_peer_id_for_stable_id`, line 244).
> **Decision:** adopt the `key=value` form (see *Decisions* below) and introduce a real
> Device layer + registry.

Peers:
 * A internal representation of a device inside rtpmidi, that is connected to a real 
   device. For example I can have a rawmidi device that rtpmidid knows about, but do
   not send data to it, so its not a peer yet. 
 * There are device peers, that kjust have a mapping from the internal rtpmidi peer 
   to a device, one to one.
 * There are also export/import peers. For example an ALSA sequencer port that when 
   connected to it via ALSA Seq creates the rtpmidi servers. Or a rtpmidi server that 
   allows connections from any client, and creates the related ALSA sequencer ports. This devices may have internal routing so from outside they receive data from any client, but internally know to send a stub peer that is rtpmidid connectged to a peer with a real device attached.

> 🔎 **Current code:** matches well. Device peers = `peer_device_alsa_seq_t`,
> `peer_device_rawmidi_t`, `peer_device_rtpmidi_client_t`, `peer_device_rtpmidi_session_t`.
> Export/import peers = `peer_export_alsa_network_t`, `peer_import_rtpmidi_t`,
> `peer_import_alsa_rtp_t`, `peer_export_rtpmidi_server_t`; the multi-listeners already
> create child peers per connection (the "stub peer" idea). The one difference: today
> all of these are the *same* `midipeer_t` type-family, not a separate Device vs Peer
> split. The new model formalizes "known device that is not yet a peer".

Peer Connections:
 * A connection between two peers (export/import or device peers). It has directionality so 
   can be one way or two ways. Internally can be converted to one way or two ways at any 
   time.
 * Can have partial stable ids, so we can connecte a partially defined device to another  
   partially defined device. This is used for durability so if one device dissapears and 
   appears with a stable compatible identifier, we can still automatically reconnect to it.
   For exmaple:
   * rtpmidi_server:name=Peak <-> rawmidi:device=/dev/snd/midiC4D0
   * rtpmidi_server:name=Peak -> alsa_seq:name=Peak,port=In
   * rtpmidi_server:name=Peak <- alsa_seq:name=Peak,port=Out
 * Internally when a MIDI message appears on one peer it is sent to the connected peers, 
   following connection directionality rules.
 * One peer can connect to several peers (N:M). So a MIDI controller can send to many synths.
 * Connections can be stored and select which parts are optional or mandatory. The format to store keeps all of latest connected peer, but mark not used parts with [], for example:
   * rtpmidi_server:name=Peak,[port=5004] -> rawmidi:device=/dev/snd/midiC4D0,[name=MIDI Export]
 * Connections can be stored and selected by the user, so we have a list of connections 
   that are stored in the database.

> 🔎 **Current code:** Router edges *are* directional — `connect(from,to)` is one-way and
> bidi = two edges (`src/midirouter.hpp:81`), and N:M is supported (`peerconnection_t::send_to`
> is a vector). **But persistence loses direction:** `normalize_sides()`
> (`connection_db.cpp:255`) sorts the pair so it is stored *undirected*, and restore always
> issues two `enqueue_connect` calls (`try_auto_connect_peer`, lines 546–547). The `[ ]`
> optional-part syntax and partial matching are **not** implemented today (schema is just
> `(side_a, side_b)`, exact match). **Decision:** store direction explicitly and implement
> partial "query" matching (see below).

Use cases:
 * A remote raspberry pi with all hardware devices aut connected to be exported to the 
   rtpmidi network.
 * A local computer recdeives those remote hardware deceis and exsport them locally as ALSA 
   sequencer ports.
 * Connect on the local computer two remote rtpmidi ports, so I can use my controller to 
   control the synths, but no touch on my raspberry pi.
 * Rememebr specific onnections so when rebooting its just connected. 
 * If the raspberry pi that exports devices is turned off, everything disconencts, but when turn on everything reconnects automatically.
 * Can connect local ALSA ports via the UI at rtpmidid.
 * Can monitor MIDI messages for both local and remote devices.
 * Can add more transports int he future as Bluetooth BLE, ipmidi, serial...
 * Can export local ALSA port via rtpmidi, when somebody connects to it, it opens the ALSA device (not before) and transmits and receives MIDI data.
 * Soem devies export an ALSA In and a Out port, we can connect one remote synth to both in one go, or only to Out.
 * Other devices have bidi ports, we can use it as bidirectional, or one direction only.

> 🔎 **Current code:** almost all use cases are already achievable —
> `[alsa_hw_auto_export]`+`hwautoannounce.cpp`, mDNS discovery + `peer_import_alsa_rtp_t`,
> `[database]`+`connection_db_manager_t` auto-reconnect on `peer_added`/`CONNECTED_PEER`
> (`connection_db.cpp:580–603`), pure-ALSA `aconnect` reconnect
> (`try_auto_aconnect_all_alsa_pairs`, line 617), web monitor (`webui_midi_monitor_peer_t`),
> and lazy device open (`peer_import_alsa_rtp_t` opens the RTP client only on ALSA connect).
> The gaps are: directionality in persistence, partial/query matching, and a Device list
> for the UI.

---

## Answers to the Doubts

**Does the model make sense? Too complex / too simple?**
It makes sense and maps cleanly onto the existing peer-router design. The hard parts are
deliberately kept: (a) the **Device** layer separate from Peer, (b) **partial "query"
matching**, and (c) **directed persistence**. None are too complex given what already
exists in `connection_db.cpp`; the work is mostly extending it.

**Does it represent real-world possibilities?**
Yes. Every use case in the list is something the daemon already does or can do with the
extensions below.

**How well does it match current software / how hard to modify (ignoring back-compat)?**
Very close. Already done: stable-ids, SQLite persistence, auto-reconnect, N:M, lazy open,
monitor. Small/medium work: `key=value` ids, directed + query columns, device list/registry,
UI. Larger/new: partial-match fan-out and the optional ALSA monitor tap.

**Can export/import peers be stored in the DB? Use cases?**
Yes — `compute_stable_id` already produces ids for `rtpmidi_server:*`, `rtpmidi_multi:*`,
`alsa_multi:*`, `alsa_listener:*`. What is stored is the **edge** between such a peer and a
device peer (e.g. "discovered *Peak* should always route into my *Network Export*
multi-listener"). The web monitor sink is explicitly **never** stored (per-session uuid).

---

## Decisions (this iteration)

1. **Keep a Device layer.** The UI receives a single list of devices coming from three
   sources: **autodiscovered** (mDNS / ALSA hw), **INI file**, and **manually added**
   (stored in the DB). Devices exist independently of whether they are currently an active
   peer.
2. **Three levels of identity:**
   * **Identity** — *all* fields of a device (fully specifies one concrete device).
   * **Query** — *only some* fields; matches **ALL** devices that satisfy it (fan-out).
   * **Stored query** — a query that is *applied* using only some fields, but **stores all
     last-seen fields** (bracketed `[ ]`) so the user can later activate/deactivate parts in
     the UI, and so we remember the original / last-seen state.
3. **Identity / id format:** `key=value` (e.g. `rtpmidi_server:name=Peak,port=5004`),
   replacing the positional form.
4. **Directionality is persisted.** Store `->`, `<-`, or `<->` and restore exactly that
   direction (no more forced bidirectional restore).
5. **Query connections fan out:** a stored query connection connects to **all** matching
   online devices.
6. **Offline devices stay listed** as "offline / last seen". They are kept in the DB so we
   know they existed, with **periodic cleanup** of discovered-only entries not seen for a
   long time (e.g. > 1 month) and not referenced by any stored connection.
7. **Storage layout:** single SQLite DB. **Extend** the existing `connections` table with
   `direction` + query columns and add a new `devices` table.
8. **Pure ALSA-seq ↔ ALSA-seq stays DIRECT** (`aconnect`, outside the router) for latency.
   These appear in the device/connection list and are managed via `aconnect`. The web
   monitor is supported via an **optional router tap inserted only while the monitor is
   open** (extra latency only during monitoring; removed on monitor stop). *If the tap
   scaffolding turns out to be too invasive, we drop monitoring for direct-ALSA links and
   keep them list-only.*

---

## Stored format examples

### Device identity (all fields)
```
alsa_seq:client=Peak,port=In
rawmidi:device=/dev/snd/midiC4D0,name=MIDI Export
rtpmidi_client:hostname=host.local,service=Peak Out,port=5004
rtpmidi_server:name=Peak InOut,port=5004
```

### Query (subset of fields — matches ALL devices that satisfy it)
```
rtpmidi_server:name=Peak            # any host, any port
alsa_seq:client=Peak                # both In and Out ports of Peak
rtpmidi_client:hostname=host.local  # every service on that host
```

### Stored query (apply some fields, remember the rest in [ ])
Bracketed `[key=value]` fields are *remembered* (last seen) but **not** used for matching;
the UI can toggle a field in/out of the brackets to activate/deactivate it.
```
rtpmidi_server:name=Peak,[port=5004]
alsa_seq:client=Peak,[port=In]
rtpmidi_client:hostname=host.local,service=Peak Out,[port=5004]
```

### Stored connection (directed, with stored queries on each side)
```
rtpmidi_server:name=Peak,[port=5004] <-> rawmidi:device=/dev/snd/midiC4D0,[name=MIDI Export]
rtpmidi_server:name=Peak            ->  alsa_seq:client=Peak,port=In
rtpmidi_server:name=Peak            <-  alsa_seq:client=Peak,port=Out
```

### Escaping
Values may contain `:` `,` `=` `[` `]`. Define a single escaping scheme (e.g. backslash
escape of those five characters) used by both serialize and parse; this replaces the
ad-hoc `:`→`|` substitution in `connection_db.cpp`.

### Proposed schema (extends existing DB)
```sql
-- extended connections table
CREATE TABLE connections (
  side_a    TEXT NOT NULL,   -- stored query (may contain [ ] fields)
  side_b    TEXT NOT NULL,
  direction TEXT NOT NULL,   -- 'a2b' | 'b2a' | 'both'
  enabled   INTEGER NOT NULL DEFAULT 1,
  PRIMARY KEY (side_a, side_b)
);

-- new devices table
CREATE TABLE devices (
  identity   TEXT PRIMARY KEY,  -- full key=value identity
  type       TEXT NOT NULL,
  name       TEXT,              -- display name
  source     TEXT NOT NULL,     -- 'discovered' | 'ini' | 'manual'
  first_seen INTEGER NOT NULL,  -- unix ts
  last_seen  INTEGER NOT NULL
);
```
`online` is **not** stored; it is computed at runtime by matching live peers/announcements
against `devices.identity`.

---

## Implementation plan (TDD)

Follow the repo TDD workflow (write failing test → minimal impl → refactor). New unit tests
go under `tests/` and are wired into `make test`.

**Status legend** (update the `Status:` line of each phase as work progresses):
`☐ Not started` · `◐ In progress` · `☑ Completed`

| # | Phase | Status |
|---|-------|--------|
| 1 | Naming & shared abstractions | ☑ Completed |
| 2 | Identity grammar (`key=value`) | ☑ Completed |
| 3 | Query matching | ☑ Completed |
| 4 | Device registry | ☑ Completed |
| 5 | Persisted connections v2 (directed + query) | ☑ Completed |
| 6 | Control + Web UI | ☑ Completed |
| 7 | Pure-ALSA direct + optional monitor tap | ☐ Not started |
| 8 | Lifecycle / cleanup | ☐ Not started |

### Engineering principles (apply to EVERY phase)

These are hard constraints, not suggestions. Take the time to find the right design before
coding each phase — the goal is the **smart long-term solution**, never a fast hack that we
have to unwind later. If a phase seems to need a hack, stop and redesign.

* **Think long term.** Prefer designs that stay correct as transports (BLE, ipmidi, serial)
  and peer types grow. Avoid special-casing that leaks across layers. A little extra design
  now beats a migration later.
* **Clean Code (Uncle Bob).** Short functions that each do one thing at a **single level of
  abstraction**. A high-level function reads like prose and delegates to named helpers;
  low-level details live in their own small functions. No long multi-purpose methods, no
  mixing of "what" and "how" in the same function. Name things for intent, not mechanism.
* **One concept per type.** Identity, query, stored-query, device, and connection are
  distinct types with distinct responsibilities — do not collapse them into stringly-typed
  blobs passed around. Parse at the boundary, work with typed values inside.
* **Use the existing queue/event system correctly.** All router state lives on the router
  thread and all peer state on the peer thread (`AGENTS.md` → *Queues and locking*). New
  operations must be **typed messages** on the appropriate priority queue
  (`router_command_t` / `peer_command_t`), never a new shared mutex or a generic
  "run-this-lambda". Reads go through a `reply_channel_t` query; reactions to topology
  changes hook the existing signals (`connected_event`, `peer_added_event`,
  `peer_event`, …) which already fire on the router thread. The device registry and
  connection manager must integrate through these mechanisms, not bypass them.
* **No blocking on hot threads.** Device/connection bookkeeping, DB writes, and matching run
  off the poller and off the MIDI HIGH path (see *Performance Considerations* in `AGENTS.md`).
* **Test-first per phase.** Each phase lands its tests with the code, green before moving on.

### Phase 1 — Naming & shared abstractions (foundational refactor)
**Status:** ☑ Completed

Done first so later phases build on clear names. Pure refactor: behavior-preserving, covered
by the existing test suite (no behavior change ⇒ tests stay green).

* **Clarify device-peer names.** Rename the one-to-one device peers to read directly and
  consistently (e.g. an ALSA-seq port peer, a rawmidi-device peer, an RTP-MIDI client peer).
  Pick names that say *what the peer is*, not historical implementation detail. Keep the
  rename mechanical and complete (declarations, factory functions in `factory.cpp`, control
  command type strings, `get_type()` values, and any `compute_stable_id`/status mapping).
* **Unify export/import peers under shared terminology.** The "creates servers/ports on
  connection" peers (`peer_export_alsa_network_t`, `peer_import_rtpmidi_t`,
  `peer_import_alsa_rtp_t`, `peer_export_rtpmidi_server_t`) are *exporters* (expose a local
  thing to the outside) and *importers* (bring an outside thing in). Introduce a common base
  — `peer_xport_t` (or the pair `peer_export_t` / `peer_import_t` if their behavior diverges
  enough) — to share the duplicated "spawn child peer + wire router edges" scaffolding. The
  base owns the common lifecycle; subclasses implement only the transport-specific bits, each
  as a small overridden method.
* **Keep `get_type()` strings stable-ish via a mapping layer.** Because control commands and
  stable-ids key off type strings, route the rename through a single mapping point so the
  on-the-wire/in-db identifiers are chosen deliberately (not accidentally tied to the C++
  class name). This is also what Phase 2/4 build the `device_identity_t` type prefix on.
* **Tests:** existing suite must stay green; add a small `tests/test_factory_naming.cpp`
  (or extend an existing factory test) asserting each factory yields the expected
  `get_type()` / identity prefix, so future renames are caught.

### Phase 2 — Identity grammar (`key=value`)
**Status:** ☑ Completed

* New `device_identity_t` (parse/serialize, ordered fields, escaping).
* **Tests** (`tests/test_device_identity.cpp`): round-trip serialize/parse; field order
  canonicalization; escaping of `: , = [ ]`; bracketed fields preserved.

### Phase 3 — Query matching
**Status:** ☑ Completed

* `query_t::matches(const device_identity_t&)` — partial match; bracketed fields ignored for
  matching but retained for storage. `find_all_matching(query, devices)`.
* **Tests** (`tests/test_device_query.cpp`): single match; multi-match fan-out; optional
  field omitted matches all; bracketed field does not narrow; mismatch.

### Phase 4 — Device registry
**Status:** ☑ Completed

* `device_registry_t`: merges sources (discovery, INI, manual/db); tracks `first_seen` /
  `last_seen` / `online`; emits change signals for the UI.
* **Queue/event integration:** the registry reacts to the router's existing signals
  (`peer_added_event`, `peer_event`, `connected_event`, `disconnected_event`) — which already
  run on the router thread — exactly like `connection_db_manager_t` does today. Reads needed
  by the registry use the existing `status_rows()` query path; no new mutex, no direct
  cross-thread access to `peers_`.
* Replace/augment `compute_stable_id()` so each `router_peer_row_t` maps to a
  `device_identity_t` (using the Phase-1 type-prefix mapping).
* **Tests** (`tests/test_device_registry.cpp`): a peer appears → device online; disappears →
  offline but still listed; INI + manual + discovered merge without duplicates; identity
  derivation for each peer type.

### Phase 5 — Persisted connections v2 (directed + query)
**Status:** ☑ Completed

* Migrate `connection_db_t` schema (Phase-4 tables); store `direction` + `enabled`.
* `connection_db_manager_t`: on restore, expand each stored query to **all** matching
  devices and connect with the stored direction only (drop forced double-connect).
* **Tests** (`tests/test_connection_db.cpp`, extend): directed restore (`->`, `<-`, `<->`);
  fan-out connect to multiple matches; disabled connection not restored; enable/disable
  round-trip.

### Phase 6 — Control + Web UI
**Status:** ☑ Completed

* RPC: `devices.list`, `connections.list` (v2: direction + enabled + query-side matching),
  `connections.save` (query + direction), `connections.enable` / `connections.disable`,
  `devices.add_manual`, `devices.remove`. Legacy `connections.add` / `connections.remove`
  remain (add stores `both` direction).
* `control_rpc_context_t` carries `device_registry`; wired from `main.cpp` through control
  socket and web server.
* Endpoint ids resolve to `key=value` identities on save when possible
  (`resolve_side_to_connection_side` in `control_rpc.cpp`).
* Frontend (`frontend/src/`):
  * **Unified device list** on Devices tab — endpoint cards merged with registry
    (`mergeDeviceList.ts`); **Online/Offline** + **source** tags on each card;
    offline-only registry rows as muted cards; **+ Add device** in toolbar.
  * **Connection editor** (`ConnectionEditorDialog`) — direction picker, endpoint picker,
    per-field active/`[ ]` toggles (`StoredQueryEditor`), enable/disable auto-reconnect.
  * Shared identity grammar in `deviceIdentity.ts` (parse/serialize/labels).
* **Tests:** `test_dm_json_generated` (new RPC structs), `test_connection_db`
  (canonicalize direction flip), `deviceIdentity.test.ts`, `persistedConnections.test.ts`.

**UI notes (Phase 7+):** Pure-ALSA direct links use `endpoint.connect` with optional bidi toggle;
saved pairs use `alsa:` / `alsa_seq:` sides and honor direction on restore. Monitor tap for direct
ALSA is active via `alsa_monitor_tap` on `monitor.start` / `monitor.stop`.

### Phase 7 — Pure-ALSA direct + optional monitor tap
**Status:** ☑ Done

* Keep ALSA↔ALSA links via `aconnect` (extend `try_auto_aconnect_all_alsa_pairs`), now
  honoring direction and stored queries.
* Monitor tap: when a monitor session targets a direct-ALSA edge, transiently route a router
  copy via the existing `webui_midi_monitor_peer_t` path; tear down on `monitor.stop`.
* **Tests:** direct-pair restore honoring direction; tap insert/remove leaves the direct link
  intact. *(If the tap proves too invasive, mark direct-ALSA links as non-monitorable and
  document it — this requirement is droppable.)*

### Phase 8 — Lifecycle / cleanup
**Status:** ☐ Not started

* Periodic sweep: delete `source='discovered'` devices with `last_seen` older than a
  threshold (e.g. 1 month) that are not referenced by any stored connection.
* **Tests:** stale discovered device pruned; referenced/ini/manual devices retained.
