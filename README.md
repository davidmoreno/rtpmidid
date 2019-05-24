# RTP MIDI User Space Driver Daemon for Linux

## Real Time Protocol Musical Instrument Digital Interface Daemon

Alpha software. Use at your own risk.

`rtpmidid` allows you to share ALSA sequencer devices on the network using RTP MIDI, and import other network shared RTP MIDI devices.

`rtpmidid` is an user daemon, and when a RTP MIDI device is announced using mDNS (also known as Zeroconf, Avahi, and multicast DNS) it exposes this alsa sequencer port.

## Other implementations

There is many hardware with RTP MIDI support, normally controllers, but for
example Behriger DeepMind12 Synthetizer has support over Wifi.

### Linux

On linux there is [raveloxmidi](https://github.com/ravelox/pimidi), but
is not intented as a ALSA sequencer bridge but directly to use on your hoardware
devices.

rtpmidid pretends to be a zero configuration daemon to have always on.

### MacOS

On MacOS there is support included in the OS.

### Windows

On windows there is
[rtpmidi](https://www.tobias-erichsen.de/software/rtpmidi.html) by Tobias
Erichsen.

### Mobiles / tablets

There are also many Apple iPad / iPhone and Android programs with rtp midi
support. I have only access to Android, and I have tested it with
[TouchDAW](https://www.humatic.de/htools/touchdaw/).


## How it works / How to use rptmidid

It provides two modes of operation, one focused on importing another on
exporting.

### Importing

For each found mDNS item, and connections to port 5004 (by default), it creates
alsa seq ports which provide the events on those endpoints.

For mDNS discovered endpoints, the connection is delayed until the alsa seq
connection.

Also it can connect to other endpoints by ip and port. Right now it is only
possible at but in the future via some other running mechanism, as watching for
a config file changes.

### Exporting

To export a local alsa sequencer port, connect it to the "Network" port.

It will create the server, announce the mDNS service so other endpoints know it
exist, and accept connections.

MIDI data is rerouted to the proper endpoint by checking the source port.

When connecting the Network port to an input, it merges all the midi events
from all exported services. Normally this is not the desired behaviour, so it is
recomended to export output ports, not input ones. This will be fixed in the
future.

## Goals

* [x] Daemon, no need for UI
* [x] ALSA MIDI -- Inputs as ALSA MIDI inputs, outputs a ALSA MIDI outputs
* [x] Autoconfigurable for mDNS Endpoints

## Features and status

Development status / plan:

* [x] mDNS client/server
* [x] Async framework (epoll with callbacks based code)
* [x] mDNS publish and learn when there are rtpmidi running on the network
* [x] Create the alsa ports for found rtpmidi servers in the network
* [x] When there is any connection to the alsa sequencer port rtpmidid creates
      the rtpmidi connection
* [x] Any message from alsa seq is sent to the MIDI rtp extreme
* [x] Any message from rtpmidi is replayed on the alsa seq
* [x] Can export local ports, with user deciding which ones to export.
* [x] Server mode at a known port, when remote side request connections, create
      the alsa seq virtual port for that connection and connect both ports.
* [x] Allow several connections on the server port. Each its own aseq port.
* [x] Send all MIDI events to rtpmidi
* [x] Receive all MIDI events from rtpmidi
* [ ] Periodic check all peers are still on, no new peers
* [x] Remove ports when peer dissapears
* [x] Client send CK every minute
* [ ] Jack MIDI support instead of ALSA seq
* [ ] Use rtp midi timestamps
* [ ] Journal support for note off
* [ ] Journal support for CC
* [ ] Journal support for Program Change
* [ ] Journal support for Pitch Bend


## License

RtpMidid is GPLv3 licensed. This bascially meass that you are free to use,
modify and share it, given that you share your modifications with your users.

This includes embeded software uses (anti
[tivoization](https://en.wikipedia.org/wiki/Tivoization) clause of the GPLv3).

If you think the license does not fit your use case, please contact me at
dmoreno@coralbits.com for alternative licensing options. I'm a freelancer
looking for new projects.
