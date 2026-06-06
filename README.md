# rtpmidid

**Real Time Protocol Musical Instrument Digital Interface Daemon** for Linux.

Alpha software — use at your own risk.

rtpmidid bridges **ALSA MIDI** and **network RTP-MIDI**: share local instruments
on the LAN, use remote RTP-MIDI gear as local ALSA ports, and route between them.
Discovery uses **mDNS** (Avahi / Bonjour).

## Install

Recommended: [Debian packages from releases](https://github.com/davidmoreno/rtpmidid/releases).

From source or Docker: see [Contributing](CONTRIBUTING.md) and
[Docker](docs/user/docker.md).

## Use the Web UI (recommended)

With the default configuration, open in a browser:

```text
http://127.0.0.1:8089
```

From there you can:

- See **Devices** (local, network, offline remembered)
- **Connect** endpoints and save **Connections** for auto-restore
- **Monitor** MIDI on a route
- Inspect **mDNS** services and live **Peers**

Guide: [docs/user/web-ui.md](docs/user/web-ui.md)

## Everyday ALSA workflow

After install, rtpmidid usually provides:

| ALSA port | Role |
|-----------|------|
| Ports per discovered network service | Import remote RTP-MIDI |
| **Network Export** | Connect local outputs here to share them on the network |
| Ports per incoming network connection | Created when something connects to your host listener |

Use `aconnect`, your DAW, or QjackCtl as with any ALSA client named `rtpmidid`.

- **Export local MIDI:** connect a local **output** → **Network Export**
- **Use remote MIDI:** subscribe your app to the discovered/import port

More: [docs/user/configuration.md](docs/user/configuration.md)

## Configuration

Main file: `/etc/rtpmidid/default.ini` (example in the repo:
[`default.ini`](default.ini)).

Enable persistence for saved connections:

```ini
[database]
path=/var/lib/rtpmidid/rtpmidid.db
```

## Optional CLI

```shell
cli/rtpmidid-cli.py status
cli/rtpmidid-cli.py connect MySynth 192.168.1.100 5004
```

[docs/user/control.md](docs/user/control.md)

## Help

| Topic | Document |
|-------|----------|
| Setup and INI | [docs/user/configuration.md](docs/user/configuration.md) |
| Web UI | [docs/user/web-ui.md](docs/user/web-ui.md) |
| Problems | [docs/user/troubleshooting.md](docs/user/troubleshooting.md) |
| Docker | [docs/user/docker.md](docs/user/docker.md) |
| All documentation | [docs/README.md](docs/README.md) |
| Developers | [AGENTS.md](AGENTS.md) |

## librtpmidid

Embeddable RTP-MIDI library (LGPL 2.1): [README.librtpmidid.md](README.librtpmidid.md).

## Bug reports

https://github.com/davidmoreno/rtpmidid/issues/

## License

- **rtpmidid** daemon — GPLv3
- **librtpmidid** — LGPL 2.1

Contact: dmoreno@coralbits.com
