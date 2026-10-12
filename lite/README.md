# rtpmidid-lite

A deliberately small RTP-MIDI daemon: **one** ALSA sequencer port exported to
the network, built only on top of `librtpmidid`.

Everything that arrives at the ALSA port is sent to every RTP-MIDI connection,
and everything that arrives from the network is written to the ALSA port. There
is no routing, no per-peer ALSA ports and no control socket: one port in, one
port out.

## Usage

```sh
rtpmidid-lite --name "My Port" --port 5004 --connect 192.168.1.20:5004
```

Then wire your applications to the ALSA port with `aconnect`:

```sh
aconnect 'My App':0 'My Port':0        # send to the network
aconnect 'My Port':0 'My Synth':0      # receive from the network
```

Options:

| Option | Meaning |
|---|---|
| `--name NAME` | ALSA client/port name and mDNS service name. Default: the system hostname. |
| `--port PORT` | UDP control port to listen on; the MIDI port is `PORT+1`. Default `5004`. |
| `--connect HOST[:PORT]` | Connect to a remote peer. Repeatable. Default port `5004`. |
| `--list` | Discover RTP-MIDI peers with mDNS, print them to stdout and exit. |
| `--list-timeout SECONDS` | How long to browse for `--list`. Default `3`. |
| `--no-mdns` | Do not announce or browse with mDNS. |
| `--log-level LEVEL` | `debug`, `info`, `warning` or `error`. |
| `-h`, `--help` / `-v`, `--version` | Help / version. |

`--list` prints the peer list to stdout and the log lines to stderr, so it can
be piped:

```sh
rtpmidid-lite --list --list-timeout 2 2>/dev/null
# RTP-MIDI peers:
#   My Port  myhost.local:5004
```

## Notes

- Only the Apple flavour of RTP-MIDI is implemented: `'RS'` receiver feedback
  in and out, no RTCP/EHSNR. This comes from `librtpmidid`.
- The daemon announces itself over mDNS (`_apple-midi._udp`) so other peers and
  `--list` can find it. Avahi being slow to start is handled by retrying the
  announcement for a few seconds.
- SysEx coming from ALSA is split into RFC 6295 segments of at most 256 data
  octets, so long messages fit in one RTP-MIDI packet each.
