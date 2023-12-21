# Architecture

These are some random notes on the rtpmidi internal architecture.

## `midipeer_t`

Internally all connections to ALSA, to network, to rtpmidi... anything that can
send or receive MIDI are a `midipeer_t`. Each one has a specific mission and
they coordinate to accomplish all the possible actions.

All `midipeer_t` are stored (`std::shared_ptr`) at the `midirouter_t` that
also is in charge of keeping tabs on connections and resending midi packets
from one peer to the connected ones.

## `local:alsa:multi:listener`

Exports an ALSA `Network` port. Any ALSA connection there creates a new
`network_rtpmidi_listener_t` server. Data is properly routed to/from that
`network_rtpmidi_listener_t`.

It does an impersonated send of data to the `network_rtpmidi_listener_t` and from
it to the right ALSA port.

## `local:alsa:waiter`

Created manually or via mDNS/Avahi/Bounjour it creates a local ALSA port
which, when a connection is kept, connects to a remote rtpmidi server.
Connects to a `network_rtpmidi_client_t`.

## `local:alsa:listener`

Just redirects MIDI to/from an ALSA port. Used by `network_rtpmidi_multi_listener_t`

## `network:rtpmidi:client`

Stores an `rtpclient_t` RTP client that connects to a remote peer and redirecs all
MIDI data as needed.

It's used by `local_alsa_waiter_t` and is ver similar to `network_rtpmidi_peer_t`, but this
is for a `rtpmidiclient_t`.

## `network:rtpmidi:multi:listener`

Creates a RTP server port on which whenever an RTP peer connects creates
both a `local_alsa_worker_t` and a `network_rtpmidi_peer_t` and connects them.

It does not send nor receive MIDI data.

It does the network interfacing for several `network:rtpmidi:server`, as it
receives the connection, but then the data is sent to the worker.

Maybe it can be removed as a `midipeer_t` as it does not really send nor receive
midi to/from the router. It instead has a list of the workers and send the
raw network data to them, and they parse it and do whatever.

## `network:rtpmidi:listener`

Exports a RTP MIDI port and mDNS, so that any external rtpmidi client can
connect. All connected peers send/receive data from this peer.

## `network:rtpmidi:server`

A connection from a rtppeer_t that send/receives MIDI.
