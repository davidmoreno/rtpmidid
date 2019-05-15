# RTP MIDI User Space Driver Daemon for Linux

## Real Time Protocol Musical Instrument Digital Interface Daemon

Still not working.

The idea is to have the daemon always running, and it will create alsa seq
points for Network devices, and export all your seq devices into the network for
external consumption.

This also allows to use ALSA seq routing to route between your devices.

Right now I'm just exploring RTP MIDI protocol and options with single
connections.

Initial protoype is in Python, future daemon will be in C, C++ or Rust.

## How to use it.

rtpmidid creates a sequencer cliet with all found external ports to be just
connected locally.

To export local MIDI ports,  there is a special port "Export A". Connect a
sequencer client to this port and it will be exported as RTPMIDI.

After connecting to export port, a new "Export B" will appear and so on.

Several clients can be connected to the same export, and if you want to use
both input and input from the same port, you have to connect to both sides to
the same export port.

## Goals

* [ ] Daemon, no need for UI
* [ ] RTP Hub -- Connects RTP clients between them
* [ ] ALSA MIDI -- Inputs as ALSA MIDI inputs, outputs a ALSA MIDI outputs
* [ ] Autoconfigurable for Avahi Endpoints
* [ ] Configurable for external non Avahi endpoints


## V0 in python

To understand the protocol, ease of development

Receives the remote IP and port, and creates the seq bridges.

## V1 in C++ (WIP)

Development status / plan:

* [x] mDNS client/server
* [x] Async framework (epoll with callbacks based)
* [x] mDNS publish and learn when there are rtpmidi running on the network
* [x] Create the alsa ports for found rtpmidi servers in the network
* [x] When there is any connection to the alsa sequencer port rtpmidid creates
      the rtpmidi connection
* [x] Any message from alsa seq is sent to the MIDI rtp extreme
* [x] Any message from rtpmidi is replayed on the alsa seq
* [x] Can export local ports, with user deciding which ones to export.
* [x] Server mode at a known port, when remote side request connections, create
      the alsa seq virtual port for that connection and connect both ports.
* [ ] Allow several connections on the server port. Each its own aseq port.
* [x] Send all MIDI events to rtpmidi
* [x] Receive all MIDI events from rtpmidi
* [ ] Periodic check all peers are still on, no new peers
* [x] Remove ports when peer dissapears
* [x] Client send CK every minute
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
dmoreno@coralbits.com for alternative licensing options.
