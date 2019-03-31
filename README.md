# RTP MIDI User Space Driver Daemon for Linux

## Real Time Protocol Music Industry Digital Interface Daemon

Still not working.

The idea is to have the daemon always running, and it will create alsa seq
points for Network devices, and export all your seq devices into the network for
external consumption.

This also allows to use ALSA seq routing to route between your devices.

Right now I'm just exploring RTP MIDI protocol and options with single
connections.

Initial protoype is in Python, future daemon will be in C, C++ or Rust.

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
* [ ] Create the alsa ports for found rtpmidi servers in the network
* [ ] When there is any connection to the alsa sequencer port rtpmidid creates
      the rtpmidi connection
* [ ] Any message from alsa seq is sent to the MIDI rtp extreme
* [ ] Any message from rtpmidi is replayed on the alsa seq
* [ ] All local ports are offered as rtpmidi connections.
* [ ] Create a black list not to export
* [ ] Optionally create a config file to avoid some exports
* [ ] When remote side request connections, create the alsa seq virtual port
      for that connection (if it does not exist from an announcement) and
      connect both ports
* [ ] Send all MIDI events to rtpmidi
* [ ] Receive all MIDI events from rtpmidi
