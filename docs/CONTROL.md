# Control protocol

rtpmidi can be controlled using a UNIX socket file. By default it is at
`/var/run/rtpmidid/control.sock`.

WARNING: This directory may not exist or not have proper permissions. The
final location can be set at execution time with `./rtpmidid --control PATH`
or create and allow the default location with (replace USER with your username):

```shell
sudo mkdir /var/run/rtpmidid/
sudo chown :USER /var/run/rtpmidid/
```

Current there is only a line based protocol: User writes a command with some
parameters and a JSON answer is returned.

In the future a full JSON RPC protocol might be used.

## Implementation note

Since 26.01, responses and command params are typed C++ structs decorated
with `/// [JSON-DM]` and serialized by the json-dm runtime
(see `docs/JSONDM.md`); nlohmann/json is no longer used. The wire protocol
itself is unchanged and byte-compatible.

## CLI

There is a very basic CLI at `cli/rtpmidid-cli.py` that receives command line
arguments and are sent as commands:

```shell
cli/rtpmidid-cli.py help
cli/rtpmidid-cli.py stats
cli/rtpmidid-cli.py connect Synth 192.168.1.13 3000
```

or the three in one, separate them with a single `.`:

```shell
cli/rtpmidid-cli.py help . stats . connect Synth 192.168.1.13 3000
```

## Line based protocol example:

```shell
$ rlwrap nc -U /tmp/rtpmidid.sock
stats
{
    "version": "0.0.1",
    "uptime": 200.0,
    "clients": [

    ],
    "servers": [

    ]
}
```

Hint: `rlwrap` allows to use readline on any command that uses stdin, as netcat.

If there is an error the `error` key will be present, and an optional code.

# Commands

## help

Shows some help about supported commands.

## stats

Shows stats about the current connections.

## status

Returns the daemon status as JSON. Behavior note (lazy connections): the
`router` list shows **live sessions only** — outbound connections
(`[connect_to]` and discovered remotes) exist as waiting ALSA ports until
an ALSA client subscribes, and are not peers of the router while waiting.
The waiting/exported endpoints are listed in the additive `exports`
section instead:

```json
{
  "version": "...",
  "settings": { "alsa_name": "rtpmidid", "control_filename": "..." },
  "router": [ { "...": "per-peer typed status" } ],
  "mdns": { "status": "Available", "announcements": [], "remote_announcements": [] },
  "exports": {
    "waiting": [ { "name": "DeepMind12D", "target": "192.168.1.33:5004", "state": "waiting", "port": 0 } ],
    "network": [ { "name": "myhost", "target": "", "state": "listening", "port": 5004 } ],
    "rawmidi": [ { "name": "MIDI C4D0", "target": "/dev/snd/midiC4D0", "state": "listening", "port": 5104 } ],
    "seq":   [ { "name": "Synth MIDI 1", "target": "16:0", "state": "connected", "port": 5106 } ]
  }
}
```

`exports` buckets: `waiting` (connect_to / discovered remotes and their
target address), `network` (generic `[rtpmidi_announce]` servers),
`rawmidi` (server-mode rawmidi exports, name and device path) and `seq`
(auto-exported sequencer ports, name and local port). Entry states:
`waiting`, `subscribed`, `connected`, `listening`. The `router` and
`mdns` sections are unchanged.

## quit | exit

Stops rtpmidid

## create host | create host port | create name host port

Creates the ALSA PORT for this HOST:PORT

PORT is 5004 by default.

Its possible to later connect something to this port so the real RTPMidi
connection is created.

# Events

The server might send asynchornous events on some moments, for subscribed
events (as connections, latency updates) or special events as shutdown.

They will be identified by "event" and then a "code" and "detail".

Example:

```json
{ "event": "close", "detail": "Shutdown", "code": 0 }
```

Known events:

## 0. shutdown

Server is shutting down and will not receive more commands.

## 1. message too long

There is limited size for commands to be received by the server, if too long
this message is sent.

# Tips and tricks

## Use jq to show some data, csvlook as table

```sh
rtpmidid-cli /tmp/rtpmidid.sock status | jq -r "[\"id\", \"name\", \"type\", \"recv\", \"sent\", \"address\", \"send_to\", \"status\", \"latency_ms\"],(.result.router[]|[.id, .name, .type, .stats.recv, .stats.sent, .peer.remote.hostname // \"-\", .send_to[0], (.peers[0].status // .peer.status // (.peers|length)), (.peer.latency_ms // .peers[0].latency_ms)]) | @csv" |csvlook
```
