# librtpmidid

librtpmidid allows to easy adding rtpmidi capabilities to your own
free or commercial project.

The main element is the class rtpmidid::rtppeer, which allows to
create peers, be it clients or servers.

The rtpserver and rtpclient are ready to use client and server
as used on rtpmidid daemon, and do all the required IO, using the
rtpmidid::poller_t.

# rtpmidid::poller_t

The poller can be reimplemented as needed in other projects, but it may
require recompilation from source code.

# rtpmidid::rtppeer

The flow of a basic rtppeer can be seen on the `test_rtppeer.cpp` file.

It mainly needs to be initalized and conenct both the command and control
ends, and then can receive the data buffers, and will call the `send_event`
when needs to send some events, normally due to a `send_midi` call (but
can be `send_ck0`)

# rtpmidid::rtpclient and rtpmidid::rtpserver

These are the network IO handlers. It receives the data as needed to create
the client or server (ip and port) and then it handles all using the
`rtpmidid::poller_t`.
