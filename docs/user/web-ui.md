# Web UI

When `[web]` is enabled in the INI (default in [`default.ini`](../../default.ini)),
rtpmidid serves a browser-based control panel.

## Open the UI

```text
http://127.0.0.1:8089
```

Default listen address is `127.0.0.1`. Change `[web] listen` and `port` to bind
elsewhere (e.g. LAN access). Use `username` / `password` if the UI should be
protected.

## Tabs

| Tab | What you can do |
|-----|-----------------|
| **Devices** | See local and network endpoints (online/offline). Connect, disconnect, monitor MIDI. Add manual devices. |
| **Connections** | View and edit saved routes. Set direction (one-way or both ways). Enable/disable auto-reconnect. |
| **Peers** | Live router view — active peers, packet counts, latency. |
| **mDNS** | Services announced and discovered on the network. |
| **Actions** | Quick connect/disconnect and utilities. |
| **Settings** | UI theme preferences. |
| **About** | Version and daemon info. |

Requires `[database] path=…` for full **Devices** (offline memory) and
**Connections** persistence.

## Typical workflow

1. Open **Devices** — check what is online (Discovered, Config, Manual tags).
2. Pick two endpoints → **Connect** (or use **Connections** to save a permanent route).
3. Use **Monitor** on a device to watch MIDI bytes (opens a fullscreen view).
4. In **Connections**, save the route with direction and enable auto-reconnect.

## MIDI monitor

1. On a device card, choose **Monitor**.
2. A fullscreen view opens (`#monitor?uuid=…` in the URL).
3. Raw MIDI traffic routed **through rtpmidid** appears there.

Pure ALSA `aconnect` links (kernel subscription only) may not show traffic unless
a monitor tap is active — see [Troubleshooting](troubleshooting.md).

## Compared to ALSA tools

The Web UI does not replace `aconnect` or your DAW's port list. It manages
**rtpmidid's routing graph** and **saved connections**. Normal MIDI workflow
still uses ALSA ports the daemon creates.

## CLI alternative

The same backend is available via Unix socket and `rtpmidid-cli` — see
[Control and CLI](control.md). Developers: [development/frontend.md](../development/frontend.md).
