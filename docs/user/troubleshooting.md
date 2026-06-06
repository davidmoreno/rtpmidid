# Troubleshooting

## No Web UI in the browser

| Check | Action |
|-------|--------|
| `[web] enabled=true` | In your INI file |
| Correct URL | `http://127.0.0.1:8089` (or your `listen`/`port`) |
| Frontend built | Packages include assets; from source run `./packaging/build-frontend.sh` and set `root=frontend/dist` |
| Auth | If `username`/`password` set, browser will prompt for credentials |

## No ALSA ports from rtpmidid

| Check | Action |
|-------|--------|
| Daemon running | `systemctl status rtpmidid` or check process |
| ALSA client visible | `aconnect -l` — look for client `rtpmidid` (or your `alsa_name`) |
| mDNS discovery | `[rtpmidi_discover] enabled=true`; Avahi running on host |
| Firewall | UDP port 5004 (and ephemeral ports) open on LAN |

## Network service visible but no connection

Discovery creates an ALSA port; RTP connects **lazily** when you subscribe that
port (e.g. `aconnect` from your app). Connect the ALSA port before expecting MIDI.

## Exported local MIDI not reaching network

- Connect the local port's **output** to **Network Export**, not its input.
- Connecting an input to Network Export merges all sources (usually unwanted).

## Saved connection does not restore after reboot

| Check | Action |
|-------|--------|
| `[database] path=…` set | Persistence disabled if empty |
| Connection **enabled** | Web UI Connections tab or `connections.enable` |
| Endpoints **online** | Query sides match when devices reappear |
| Direction | `a2b` / `b2a` / `both` must match intent |

List saved rows: Web UI **Connections** or `connections.list` via CLI.

## MIDI monitor shows nothing

Monitor sees traffic that flows through **rtpmidid router edges**. It will not
show MIDI that only uses kernel `aconnect` between two ALSA ports unless a
monitor session created a temporary tap.

Use **Devices → Monitor** on an endpoint that is part of an rtpmidid route.

## High latency or dropouts

- Prefer Ethernet over Wi‑Fi for RTP-MIDI.
- Journal recovery is not implemented — packet loss on bad networks can lose
  note-offs and CC (see [development/development-notes.md](../development/development-notes.md)).

## Docker-specific issues

See [Docker](docker.md) (D-Bus, mDNS, `/dev/snd` permissions).

## Collecting debug info for a bug report

```bash
rtpmidid --ini your.ini 2>&1 | tee rtpmidid.log
cli/rtpmidid-cli.py status
make capture PORT=5004    # from source tree; network capture
```

Report at: https://github.com/davidmoreno/rtpmidid/issues/
