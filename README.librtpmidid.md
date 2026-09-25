# librtpmidid

librtpmidid allows to easy adding rtpmidi capabilities to your own free or
commercial project.

The main element is the class rtpmidid::rtppeer, which allows to create peers,
be it clients or servers.

The rtpserver and rtpclient are ready to use client and server as used on rtpmidid 
daemon, and do all the required IO, using the
rtpmidid::poller_t.

In all classes it tries to expose al internal state as can be needed for
extending funciontalities.

Also a `rtpmidid::signal_t` is used all around as a subscription service to
events.

## rtpmidid::poller_t

The poller can be reimplemented as needed in other projects, but it may require
recompilation from source code.

## rtpmidid::rtppeer

The flow of a basic rtppeer can be seen on the `test_rtppeer.cpp` file.

It mainly needs to be initialized and connect both the command and control
ends, and then can receive the data buffers, and will call the `send_event` when
needs to send some events, normally due to a `send_midi` call (but can be
`send_ck0`)

## rtpmidid::journal_codec_t and rtpmidid::recovery_journal_t

RFC 6295 recovery journal, Chapter N (MIDI NoteOn `0x9` / NoteOff `0x8`), the
chapter that repairs missing note on/off after packet loss.

- `journal_codec_t`: stateless wire codec. Encode with `write_journal_n()` /
  `write_chapter_n()`, decode with `read_journal()` / `read_chapter_n()`.
- `recovery_journal_t`: session state, both directions. Receive:
  `observe()` classifies each incoming packet, `midi_played()` records what was
  played, and `parse_journal()` repairs a detected loss. Send: `midi_out()`
  records the commands of each outgoing packet and `write_journal()` produces the
  journal section to append to it (set the `J` bit), with the checkpoint driven
  by `'RS'` feedback via `feedback_in()`.

`rtppeer_t` already owns one and uses it for incoming packets. See
[docs/architecture/recovery-journal.md](docs/architecture/recovery-journal.md),
`test_journal.cpp` and `test_recovery_journal.cpp`.

## rtpmidid::rtpclient and rtpmidid::rtpserver

These are the network IO handlers. It receives the data as needed to create the
client or server (ip and port) and then it handles all using the
`rtpmidid::poller_t`.

# Tips

## CK0

As client you must follow some extra dance after connecting, as seen at
`rtpclient::connected`:

- Send 6 ck0 in 1500ms intervals
- Send ck every 10 seconds

Failure to do this as a client may result in disconnect from Mac OS and Tobias
Erichsen Rtpmidi for Windows.

## mDNS

At first we had a custom implementation. Bad idea. Better just use avahi.

Also if the computer has several network interfaces it may return several IPS to
connect to. Try them all. The order may be better first ehternet than Wifi, but
just because is listed does not mean it will work.
