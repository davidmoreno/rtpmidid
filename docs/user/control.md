# Control and CLI

Optional runtime control via Unix socket or the `rtpmidid-cli` helper. The
**Web UI** is the recommended interface for most users — see [Web UI](web-ui.md).

## Socket location

Default: `/var/run/rtpmidid/control.sock`

Set in `[general] control=…` or `rtpmidid --control PATH`.

If the directory does not exist:

```shell
sudo mkdir /var/run/rtpmidid/
sudo chown :$USER /var/run/rtpmidid/
```

## CLI

```shell
cli/rtpmidid-cli.py help
cli/rtpmidid-cli.py status
cli/rtpmidid-cli.py connect MySynth 192.168.1.100 5004
```

Chain commands with `.`:

```shell
cli/rtpmidid-cli.py help . status
```

## Common tasks

| Task | CLI / RPC |
|------|-----------|
| Overview | `status` |
| Connect to remote host | `connect` with hostname (and optional port/name) |
| List ALSA ports | `midi.listAlsaSeq` |
| List raw MIDI devices | `midi.listRawMidi` |

Stop the daemon with `SIGINT` / `SIGTERM` (Ctrl+C or `systemctl stop`). There
is no remote `quit` command.

## JSON-RPC (advanced)

One JSON object per line:

```json
{"method":"status","params":{},"id":1}
```

Same protocol on WebSocket `/ws` when `[web]` is enabled.

Full command reference: [development/control-protocol.md](../development/control-protocol.md).
