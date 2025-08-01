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
example Behringer DeepMind12 Synthesizer has support over Wifi.

### Linux

- [raveloxmidi](https://github.com/ravelox/pimidi) - Intended to be used
  directly with hardware devices or emulate midi over FIFO.
- [rtpmidi](https://mclarenlabs.com/rtpmidi/) from [McLaren
  Labs](https://mclarenlabs.com/). It is a feature complete, GUI driven,
  Comercial implementation. Very similar to Tobias Erichsen's rtpmidi.

### MacOS

On MacOS there is support included in the OS.

### iPad

To test I used `MIDI Network` and `MIDI Wrench` . The first to manage
connections, the second to test.

I was able to communicate exporting at rtpmidid side connecting my gadget to the
`Network` port. Then use the `MIDI Network` on the iPad to make the connection.

### Windows

- [rtpmidi](https://www.tobias-erichsen.de/software/rtpmidi.html) by Tobias
  Erichsen.

### Mobile / Tablet

There are also many Apple iPad / iPhone and Android programs with rtp midi
support. I will try to list them as I find them:

#### Android

- [MIDI Hub] (https://abrahamwisman.com/midihub)
- [MIDI Connector] (https://abrahamwisman.com/midiconnector)
- [TouchDAW](https://www.humatic.de/htools/touchdaw/).

## How to use `rtpmidid`

Recomended use is via debian packages, at https://github.com/davidmoreno/rtpmidid/releases

```
Real Time Protocol Music Instrument Digital Interface Daemon v23.10b1~12~g3495e
(C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
Share ALSA sequencer MIDI ports using rtpmidi, and viceversa.

rtpmidi allows to use rtpmidi protocol to communicate with MIDI equipement
using network equipiment. Recomended use is via ethernet cabling as with WiFi
there is a lot more latency. Internet use has not been tested, but may also
deliver high latency.

Options:
  --ini                          Loads an INI file as default configuration. Depending on order may overwrite other arguments
  --port                         Opens local port as server. Default 5004.
  --name                         Forces the alsa and rtpmidi name
  --alsa-name                    Forces the alsa name
  --rtpmidid-name                Forces the rtpmidi name
  --control                      Creates a control socket. Check CONTROL.md. Default `/var/run/rtpmidid/control.sock`
  --version                      Show version
  --help                         Show this help
```

By default it only exports all network discovered rtpmidi network ports
into ALSA ports.

On installation from .deb it uses a default `/etc/rtpmidid/default.ini` ini
file that sets some sensible defaults:

- Export as ALSA a "Network" port to which any ALSA connected device is
  exported to the network.
- Export as ALSA ports any rtpmidi announced on the network.
- Export a Network rtpmidi port at 5004, to which external devices can connect
  to and will be shown as ALSA ports.

If there is no ini file, must use `--alsa-name`, `--rtpmidid-name` or `--name`
to create the ALSA Network port, the rtpmidi host port, or both.

Normal rule is just connect with normal ALSA commands and UIs, and the
communication will be transparent.

### rtpmidi `{{hostname}}` port

Announced an rtpmidi service using mDNS (avahi / zeroconf) with the name
of the current host.

Other services can connect to this rtpmidi port and ALSA ports
will be created for this connection.

### ALSA `Network` port.

To export a local alsa sequencer port, connect it to the "Network" port.

It will create the server, announce the mDNS service so other endpoints know
it exist, and accept connections.

MIDI data is rerouted to the proper endpoint by checking the source port.

When connecting the Network port to an input, it merges all the midi events
from all exported services. Normally this is not the desired behaviour, so it
is recomended to export output ports, not input ones. This will be fixed in the
future.

### Automatic detection of announced rtpmidi ports

Any port announced on the network will be available at the ALSA port always.

For mDNS discovered endpoints, the connection is delayed until the alsa seq
connection.

### Direct connection

Also it can connect to other endpoints by ip and port. It's possible to create
new connection via command line with `rtpmidid-cli connect NAME IP PORT`. There
are variations to connect that skip the name and port.

### `.ini` files

Ini files can be loaded to set up initial status as described above. This the
default, here as reference:

```ini
# example ini file for rtpmidid
[general]
alsa_name=rtpmidid
control=/var/run/rtpmidid/control.sock

## All announce sections and connect_to can appear several times.

# RTPMIDI announcement requires a firewall rule to allow incoming
# connections on port 5004. If you want to not have an rtpmidi_announce
# section, comment it out or delete it.
# This creates an announced rtpmidi endpoint, and any connection
# will create a new ALSA port.
[rtpmidi_announce]
name={{hostname}}
port=5004

# Alsa announcement requires no firewall as it creates random
# ports for each connection. If you want to not have an alsa_announce
# section, comment it out or delete it.
[alsa_announce]
# Name for the ALSA connection so that when a conneciton is made, an rtpmidi
# is announced.
name=Network Export

# and now some fixed connections
# [connect_to]
# hostname=192.168.1.33
# port=5004
# name=DeepMind12D

# [connect_to]
# hostname=192.168.1.210
# # default port is 5004
# name=midid
```

## Install and Build

There are Debian packages at https://github.com/davidmoreno/rtpmidid/releases .
They are available for Ubuntu 18.04 x64 and Raspbian 32bits, but may work
also on other systems. Only the `rtpmidid_[version]_[arch].deb` file is needed.

Install with:

```
dpkg -i rtpmidid.deb
apt -f install
```

Replace the rtpmidid.deb file with the name of the downloaded file.
`apt -f install` ensure that all dependencies are installed.

To easy build there is a simple makefile, which can be invoked to compile with
`make build` . Use `make` or `make help` to get help on commands.

Its possible to create a debian / ubuntu package with `make deb`

Requires C++20 (Ubuntu 22.04+), libasound2-dev, libavahi-client-dev, libfmt-dev and ninja-build.

## Testing and bug reporting

If you find any bug, please report it to https://github.com/davidmoreno/rtpmidid/issues/

It is very usefull if you can accompany it with the resulting capture file from
executing `make capture` .

`make capture` is tuned to capture the network packets at ports `10000` and
`10001`, but can be tuned to other ports with `make capture PORT=5004`.

The port `10000` is the one used for developing. To run a development version of
the rtpmidid server, execute `make run` . There are more options at `make help`.

This captures packets for connections TO rtpmidid, not connecitons FROM rtpmidid.
For those connecitons another port may need to be set.

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

RtpMidid is GPLv3 licensed. This basically means that you are free to use,
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
