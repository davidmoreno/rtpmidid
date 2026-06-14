# Configuration

rtpmidid is configured with an INI file. Packages install
`/etc/rtpmidid/default.ini`; the repository example is [`default.ini`](../../default.ini).

## Quick start

After install, the default setup usually provides:

- An RTP-MIDI listener on port **5004** (hostname announced on the network)
- An ALSA port **Network Export** — connect local ALSA outputs here to share them
- Automatic import of RTP-MIDI services discovered on the LAN (mDNS)

Restart the daemon after editing the INI file.

## Essential sections

### [general]

```ini
[general]
alsa_name=rtpmidid
control=/var/run/rtpmidid/control.sock
log_level=info
```

`alsa_name` is the ALSA client name shown in `aconnect`, QjackCtl, etc.

### [web] — Web UI

```ini
[web]
enabled=true
listen=127.0.0.1
port=8089
root=frontend/dist
```

Open `http://127.0.0.1:8089` in a browser. See [Web UI](web-ui.md).

Optional `username` / `password` enable HTTP Basic auth on the Web UI and
WebSocket.

### [database] — remember devices and connections

```ini
[database]
path=/var/lib/rtpmidid/rtpmidid.db
```

Enables the **Devices** and **Connections** tabs in the Web UI and auto-reconnect
after restart. Leave `path` empty to disable persistence.

### [log] — queryable log buffer

```ini
[log]
buffer_capacity=1024
```

Keeps the last N log entries in memory, accessible via the **Logs** tab in the
Web UI or the `log.query` RPC method. Default capacity is 1024; valid range
256–65536.

### [peer] — endpoints to create at startup (repeatable)

```ini
[peer]
identity=rtpmidi_multi:name={{hostname}},port=5004

[peer]
identity=alsa_multi:name=Network Export
```

`{{hostname}}` is replaced with your machine name at startup.

### [connect] — routes at startup (repeatable)

```ini
[connect]
from=rawmidi:device=/dev/snd/midiC4D0,name=My Synth
to=rtpmidi_server:name=My Synth,port=5104
direction=both
```

`direction` is `a2b`, `b2a`, or `both` (default `both`).

### Discovery and auto-export

```ini
[rtpmidi_discover]
enabled=true
name_positive_regex=.*
name_negative_regex=^$

[alsa_hw_auto_export]
name_positive_regex=.*
name_negative_regex=(System|Timer|Announce)
type=hardware
```

Discovered network services appear as ALSA ports. Matching local hardware ports
can be auto-exported to RTP-MIDI.

## ALSA usage (no extra config)

- **Import from network:** discovered services show up as ALSA ports under the
  `rtpmidid` client — subscribe with `aconnect` or your DAW as usual.
- **Export to network:** connect a local ALSA **output** to **Network Export**
  (not inputs — merging inputs is usually unwanted).
- **Incoming connections:** other machines can connect to your port 5004 listener;
  each connection gets its own ALSA port.

## Command-line overrides

```text
rtpmidid --ini /path/to/config.ini
rtpmidid --name MyHost --control /tmp/rtpmidid.sock
```

Run `rtpmidid --help` for all options.

## More detail

- Full INI reference (all keys): [development/configuration.md](../development/configuration.md)
- Device identity grammar (for advanced `[peer]` / `[connect]`): [architecture/entities.md](../architecture/entities.md)
