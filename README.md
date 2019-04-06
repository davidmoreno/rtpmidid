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

## Goals

* [ ] Daemon, no need for UI
* [ ] RTP Hub -- Connects RTP clients between them
* [ ] ALSA MIDI -- Inputs as ALSA MIDI inputs, outputs a ALSA MIDI outputs
* [ ] Autoconfigurable for Avahi Endpoints
* [ ] Configurable for external non Avahi endpoints


## V0 in python

To understand the protocol, ease of development

Receives the remote IP and port, and creates the seq bridges.

## V1 may be in Rust or C
