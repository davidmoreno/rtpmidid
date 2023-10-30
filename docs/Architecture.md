# Architecture

These are some random notes on the rtpmidi internal architecture.

## `midipeer_t`

Internally all connections to ALSA, to network, to rtpmidi... anything that can
send or receive MIDI are a `midipeer_t`. Each one has a specific mission and
they coordinate to accomplish all the possible actions.

All `midipeer_t` are stored (`std::shared_ptr`) at the `midirouter_t` that
also is in charge of keeping tabs on connections and resending midi packets
from one peer to the connected ones.

## `alsalistener_t`

Exports an ALSA `Network` port. Any ALSA connection there creates a new
`rtpmidiserverworker_t` server. Data is properly routed to/from that
`rtpmidiserverworker_t`.

It does an impersonated send of data to the `rtpmidiserverworker_t` and from
it to the right ALSA port.

## `alsawaiter_t` (remove midipeer_t)

Created manually or via mDNS/Avahi/Bounjour it creates a local ALSA port
which, when a connection is kept, connects to a remote rtpmidi server.
Connects to a `rtpmidiclientworker_t`.

## `alsaworker_t`

Just redirects MIDI to/from an ALSA port. Used by `rtpmidilistener_t`

## `rtpmidiclientworker_t`

Stores an `rtpclient_t` RTP client that connects to a remote peer and redirecs all
MIDI data as needed.

It's used by `alsawaiter_t` and is ver similar to `rtpmidiworker_t`, but this
is for a `rtpmidiclient_t`.

## `rtpmidilistener_t` (remove midipeer_t)

Creates a RTP server port on which whenever an RTP peer connects creates
both a `alsaworker_t` and a `rtpmidiworker_t` and connects them.

It does not send nor receive MIDI data.

## `rtpmidiserverworker_t`

Exports a RTP MIDI port and mDNS, so that any external rtpmidi client can
connect. All connected peers send/receive data from this peer.

## `rtpmidiworker_t`

A connection to a rtppeer_t that send/receives MIDI.
