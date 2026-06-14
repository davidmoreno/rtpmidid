# Configuration

Configuration is loaded from INI files (default: `/etc/rtpmidid/default.ini`)
and command-line arguments. Example: [`default.ini`](../../default.ini).

Parser: [`src/ini.cpp`](../../src/ini.cpp). Struct: [`src/settings.hpp`](../../src/settings.hpp).

## INI sections

Only these section names are accepted; others throw `Invalid section`.

### [general]

```ini
[general]
alsa_name=rtpmidid
control=/var/run/rtpmidid/control.sock
log_level=info    # debug|info|warning|error or 0|1|2|3
```

### [peer] (repeatable)

Each block requires `identity=` with a full device identity string:

```ini
[peer]
identity=rtpmidi_multi:name={{hostname}},port=5004

[peer]
identity=alsa_multi:name=Network Export
```

`{{hostname}}` is replaced with `gethostname()` at parse time.

Legacy `id=` / `type=listen_rtpmidi` / `[bridge]` are **not** supported.

### [connect] (repeatable)

```ini
[connect]
from=rawmidi:device=/dev/snd/midiC4D0,name=MIDI Export
to=rtpmidi_server:name=MIDI Export,port=5104
direction=both    # optional: a2b | b2a | both (default both)
```

### [rtpmidi_discover]

```ini
[rtpmidi_discover]
enabled=true
name_positive_regex=.*
name_negative_regex=^$    # checked first
```

### [alsa_hw_auto_export]

```ini
[alsa_hw_auto_export]
name_positive_regex=.*
name_negative_regex=(System|Timer|Announce)
type=hardware    # hardware | software | system | all | none
```

### [web]

```ini
[web]
enabled=true
listen=127.0.0.1
port=8089
root=frontend/dist
#username=
#password=
```

Empty `root` defaults to `frontend/dist` after argv/INI load.

### [database]

```ini
[database]
path=build/rtpmidid.db
```

### [log]

```ini
[log]
# Ring buffer capacity for queryable logs (RPC log.query + Web UI Logs tab).
# Range: 256..65536.
buffer_capacity=1024
```

Empty path disables DB features (no `devices.*` / `connections.*` persistence).

## settings_t

```cpp
struct settings_t {
    std::string alsa_name;
    std::string control_filename;
    rtpmidid::logger_level_t log_level;

    std::vector<ini_peer_t> ini_peers;       // { identity }
    std::vector<ini_connect_t> ini_connects; // { from, to, direction? }
    rtpmidi_discover_t rtpmidi_discover;
    alsa_hw_auto_export_t alsa_hw_auto_export;
    web_t web;
    database_t database;
};
```

`main.cpp` creates peers from `ini_peers` and wires `ini_connects` via
`apply_ini_connects()`.

## Command-line arguments

```
--ini FILE           Load INI configuration file
--port PORT          Server port (default: 5004)
--name NAME          Set both ALSA and RTP-MIDI name
--alsa-name NAME     Set ALSA client name
--rtpmidid-name NAME Set RTP-MIDI service name
--control PATH       Control socket path
--version            Show version
--help               Show help
```

## Identity grammar

Device identity strings used in `[peer]`, `[connect]`, DB, and RPC share one
grammar. See [entities.md](../architecture/entities.md) and [peer-types.md](../architecture/peer-types.md).

## Related docs

- [discovery.md](../architecture/discovery.md) — discover and hw auto-export behavior
- [database.md](../architecture/database.md) — `[database]` persistence
- [frontend.md](frontend.md) — `[web]` UI
- [configuration (user)](../user/configuration.md) — user-facing summary
