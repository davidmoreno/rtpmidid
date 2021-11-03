% RTPMIDID(1) rtpmidid 21.11
% David Moreno <dmoreno@coralbits.com>
% November 2021

# NAME

rtpmidid -- RTP MIDI daemon that exports via ALSA sequencer interface

# SYNOPSIS

**rtpmidid** _\[options\]_ _[addresses to connect]_

# DESCRIPTION

`rtpmidid` allows you to share ALSA sequencer devices on the network using RTP
MIDI, and import other network shared RTP MIDI devices.

`rtpmidid` is an user daemon, and when a RTP MIDI device is announced using mDNS
(also known as Zeroconf, Avahi, and multicast DNS) it exposes this ALSA
sequencer port.

# OPTIONS

**\--version**
: Show version

**\--help**
: Show this help

**\--name name**
: Forces a rtpmidi name

**\--host address**
: My default IP. Needed to answer mDNS. Normally guessed but may be attached to another ip.

**\--port port**
: Opens local port as server. Default 5004. Can set several.

**\--connect address**
: Connects the given address. This is default, no need for \--connect

**\--control path**
: Creates a control socket. Check CONTROL.md. Default `/var/run/rtpmidid/control.sock`

Address for connect:

**hostname**
: Connects to hostname:5004 port using rtpmidi

**hostname:port**
: Connects to a hostname on a given port

**name:hostname:port**
: Connects to a hostname on a given port and forces a name for alsaseq

# EXAMPLES

**rtpmidid mypc.local**

Connects to mypc on startup

**rtpmidid deepmind12::192.168.1.200:5000**

Connects to the IP 192.168.1.200 port 5000, and sets the name deepmind12.
This is usefull on devices that do not export via mDNS, like the
Behringer Deepmind 12.

# EXIT VALUES

**0**
: Success

**1**
: Error starting

# SEE ALSO

**rtpmidi-cli(1)**

# COPYRIGHT

(C) 2019-2021 David Moreno <dmoreno@coralbits.com>.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
