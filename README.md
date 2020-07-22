# RTP MIDI User Space Driver Daemon for Linux

## Real Time Protocol Musical Instrument Digital Interface Daemon

Alpha software. Use at your own risk.

`rtpmidid` allows you to share ALSA sequencer devices on the network using RTP
MIDI, and import other network shared RTP MIDI devices.

`rtpmidid` is an user daemon, and when a RTP MIDI device is announced using mDNS
(also known as Zeroconf, Avahi, and multicast DNS) it exposes this ALSA
sequencer port.

## librtpmidid

For information about librtpmidid and how to use it, read
[README.librtpmidid.md](README.librtpmidid.md).

## Other implementations

There is many hardware with RTP MIDI support, normally controllers, but for
example Behringer DeepMind12 Synthetizer has support over Wifi.

### Linux

- [raveloxmidi](https://github.com/ravelox/pimidi) - Intended to be used
  directly with hardware devices or emulate midi over FIFO.
- [rtpmidi](https://mclarenlabs.com/rtpmidi/) from [McLaren
  Labs](https://mclarenlabs.com/). It is a feature complete, GUI driven,
  Comercial implementation. Very similar to Tobias Erichsen's rtpmidi.

### MacOS

On MacOS there is support included in the OS.

### iPad

To test I used `MIDI Network` and `MIDI Wrench`. The first to manage
connections, the second to test.

I was able to communicate exporting at rtpmidid side conneting my gadget to the
`Network` port. Then use the `MIDI Network` on the iPad to make the connection.

### Windows

- [rtpmidi](https://www.tobias-erichsen.de/software/rtpmidi.html) by Tobias
  Erichsen.

### Mobile / Tablet

There are also many Apple iPad / iPhone and Android programs with rtp midi
support. I will try to list them as I find them:

#### Android

- [TouchDAW](https://www.humatic.de/htools/touchdaw/).

## How to use `rtpmidid`

recomended use is via debian packages, at https://github.com/davidmoreno/rtpmidid/releases

`rtpmidid` provides two modes of operation, one focused on importing another on
exporting.

```
Real Time Protocol Music Instrument Digital Interface Daemon - 20.04.5
(C) 2019 David Moreno Montero <dmoreno@coralbits.com>
Share ALSA sequencer MIDI ports using rtpmidi, and viceversa.

rtpmidi allows to use rtpmidi protocol to communicate with MIDI equipement
using network equipiment. Recomended use is via ethernet cabling as with WiFi
there is a lot more latency. Internet use has not been tested, but may also
deliver high latency.

Options:
  --version           Show version
  --help              Show this help
  --name <name>       Forces a rtpmidi name
  --host <address>    My default IP. Needed to answer mDNS. Normally guessed but may be attached to another ip.
  --port <port>       Opens local port as server. Default 5004. Can set several.
  --connect <address> Connects the given address. This is default, no need for --connect
  --control <path>    Creates a control socket. Check CONTROL.md. Default `/var/run/rtpmidid/control.sock`
  address for connect:
  hostname            Connects to hostname:5004 port using rtpmidi
  hostname:port       Connects to a hostname on a given port
  name:hostname:port  Connects to a hostname on a given port and forces a name for alsaseq
```

### Importing

For each found mDNS item, and connections to port `5004` (by default), it creates
alsa seq ports which provide the events on those endpoints.

For mDNS discovered endpoints, the connection is delayed until the alsa seq
connection.

Also it can connect to other endpoints by ip and port. It's possible to create
new connection via command line with `rtpmidid-cli connect NAME IP PORT`. There
are variations to connect that skip the name and port.

### Exporting

To export a local alsa sequencer port, connect it to the "Network" port.

It will create the server, announce the mDNS service so other endpoints know it
exist, and accept connections.

MIDI data is rerouted to the proper endpoint by checking the source port.

When connecting the Network port to an input, it merges all the midi events
from all exported services. Normally this is not the desired behaviour, so it is
recomended to export output ports, not input ones. This will be fixed in the
future.

## Install and Build

There are Debian packages at https://github.com/davidmoreno/rtpmidid/releases .
They are available for Ubuntu 18.04 x64 and Raspbian 32bits, but may work
also on other systems.

To easy build there is a simple makefile, which can be invoked to compile with
`make build`. Use `make` or `make help` to get help on commands.

Its possible to create a debian / ubuntu package with `make deb`

Requires C++17 (Ubuntu 18.04+), and libfmt-dev, libasound2-dev, libavahi-client-dev.

## Goals

- [x] Daemon, no need for UI
- [x] ALSA MIDI -- Inputs as ALSA MIDI inputs, outputs a ALSA MIDI outputs
- [x] Autoconfigurable for mDNS Endpoints

## Roadmap: Features and status

Development status / plan:

- [x] mDNS client/server
- [x] Async framework (epoll with callbacks based code)
- [x] mDNS publish and learn when there are rtpmidi running on the network
- [x] Create the alsa ports for found rtpmidi servers in the network
- [x] When there is any connection to the alsa sequencer port `rtpmidid` creates
      the rtpmidi connection
- [x] Any message from alsa seq is sent to the MIDI rtp extreme
- [x] Any message from rtpmidi is replayed on the alsa seq
- [x] Can export local ports, with user deciding which ones to export.
- [x] Server mode at a known port, when remote side request connections, create
      the alsa seq virtual port for that connection and connect both ports.
- [x] Allow several connections on the server port. Each its own aseq port.
- [x] Send all MIDI events to rtpmidi
- [x] Receive all MIDI events from rtpmidi
- [ ] Periodic check all peers are still on, no new peers
- [x] Remove ports when peer dissapears
- [x] Client send CK every minute
- [x] Can be controlled via Unix socket, but not required.
- [ ] Jack MIDI support instead of ALSA seq
- [ ] Use rtp midi timestamps
- [ ] Journal support for note off
- [ ] Journal support for CC
- [ ] Journal support for Program Change
- [ ] Journal support for Pitch Bend

Currently there is no journal. This is **normally** not a problem on local
networks, as the thernet protocol already has packet error and detection, but
packets may arrive out of order. Anyway **its not production ready** just for
this missing feature.

## CLI Control

There is a basic CLI that used the [Unix socket control](#unix-socket-control).

Use as:

```shell
cli/rtpmidid-cli.py help
```

## License

RtpMidid is GPLv3 licensed. This basically meass that you are free to use,
modify and share it, given that you share your modifications with your users.

This includes embeded software uses (anti
[tivoization](https://en.wikipedia.org/wiki/Tivoization) clause of the GPLv3).

The librtpmidid library is LGPL 2.1 license, so it can be embedded freely
on free and commercial software.

If you think the license does not fit your use case, please contact me at
dmoreno@coralbits.com for alternative licensing options. I'm a freelancer
looking for new projects.

## Resources

- https://developer.apple.com/library/archive/documentation/Audio/Conceptual/MIDINetworkDriverProtocol/MIDI/MIDI.html
- https://www.tobias-erichsen.de/software/rtpmidi.html
- https://en.wikipedia.org/wiki/RTP-MIDI
- https://tools.ietf.org/html/rfc6295
- http://www.rfc-editor.org/rfc/rfc4696.txt
- http://john-lazzaro.github.io//rtpmidi/
