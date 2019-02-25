#!env/bin/python3
import alsaseq
import sys
import time
import socket
import struct
import random
import select
import zeroconf
import queue
import traceback
import os
import logging
import hashlib

logging.basicConfig(format='%(levelname)-8s | %(message)s', level=logging.DEBUG)

logger = logging.getLogger(__name__)
NAME = '%s - ALSA SEQ' % (socket.gethostname())
# By default las 4 bytes of sha1 of the name
SSRC = struct.unpack("<L", hashlib.sha1(NAME.encode('utf8')).digest()[-4:])[0]
PORT = 10008
DEBUG = True

task_queue = queue.Queue()
task_ready_fds = os.pipe2(os.O_NONBLOCK)
task_ready_fds = [os.fdopen(task_ready_fds[0], 'rb'), os.fdopen(task_ready_fds[1], 'wb')]
rtp_midi = None
event_dispatcher = None


def main():
    global rtp_midi, event_dispatcher, SSRC, PORT
    logger.info("RTP/MIDI v.0.2. (c) 2019 Coralbits SL, David Moreno. Licensed under GPLv3.")
    event_dispatcher = EventDispatcher()
    rtp_midi = RTPMidi()

    # zero conf setup
    zeroconfp = zeroconf.Zeroconf()
    apple_midi_listener = AppleMidiListener()
    zeroconf.ServiceBrowser(zeroconfp, "_apple-midi._udp.local.", apple_midi_listener)

    alsaseq.client("Network", 1, 1, False)

    for arg in sys.argv[1:]:
        hostport = arg.split(':')
        rtp_midi.connect_to(*hostport)
    for line in open('rtpmidid.conf').readlines():
        line = line.split('#')[0].strip().lower()
        if not line:
            continue
        print(line)
        if line.startswith('id=') or line.startswith('id ='):
            SSRC = int(line.split('=')[1].strip(), 16)
        elif line.startswith('port=') or line.startswith('port ='):
            PORT = int(line.split('=')[1].strip(), 10)
        else:
            hostport = line.split(':')
            rtp_midi.connect_to(*hostport)

    logger.info("My SSID is %X. Listening at port %d / %d", SSRC, PORT, PORT + 1)

    event_dispatcher.add(alsaseq.fd(), process_alsa)
    event_dispatcher.add(rtp_midi.filenos(), rtp_midi.data_ready)
    event_dispatcher.add(task_ready_fds[0].fileno(), process_tasks, task_ready_fds[0])

    logger.debug("Loop")
    try:
        n = 0
        while True:
            print("Event count: %s" % n, end="\r")
            event_dispatcher.wait_and_dispatch_one()
            n += 1
    except KeyboardInterrupt:
        rtp_midi.close_all()


class EventDispatcher:
    def __init__(self):
        self.epoll = select.epoll()
        self.fdmap = {}
        self.timeouts = []
        self.tout_id = 1

    def add(self, fdlike, func, *args, **kwargs):
        for fd in maybe_wrap(fdlike):
            self.fdmap[fd] = (func, args, kwargs)
            self.epoll.register(fd, select.EPOLLIN | select.EPOLLHUP | select.EPOLLERR)

    def wait_and_dispatch_one(self):
        tout = None
        timeout = None
        if self.timeouts:
            tout = self.timeouts[0]
            timeout = tout[0] - time.time()
            # logger.debug("Next timeout in %.2f secs", timeout)
            if timeout <= 0:
                tout[2](*tout[3], **tout[4])
                self.remove_call_later(tout[1])
                return

        ready = self.epoll.poll(timeout=timeout)
        if not ready and tout:
            if time.time() >= tout[0]:
                tout[2](*tout[3], **tout[4])
                self.remove_call_later(tout[1])

        for (fd, event) in ready:
            # logger.debug("Data ready fd: %d. avail: %s", fd, self.fdmap)
            f_tuple = self.fdmap.get(fd)
            if not f_tuple:
                logger.error("Got data for an unmanaged fd")
                continue

            f, args, kwargs = f_tuple
            if not args and not kwargs:
                args = (fd,)
            try:
                f(*args, **kwargs)
            except Exception:
                logger.error("Error executing: %s", f.__name__)
                traceback.print_exc()

    def call_later(self, t, func, *args, **kwargs):
        """
        Prepare a function to be called later in some seconds.
        """
        tout = time.time() + t
        id = self.tout_id
        self.tout_id += 1
        self.timeouts.append((tout, id, func, args, kwargs))
        self.timeouts.sort()
        return id

    def remove_call_later(self, id):
        idx = None
        for n, x in enumerate(self.timeouts):
            if x[1] == id:
                idx = n
        if idx is not None:
            self.timeouts = self.timeouts[1:]


def process_alsa(fd):
    alsaseq.inputpending()
    ev = alsaseq.input()
    if DEBUG:
        # logger.debug("ALSA: %s", ev_to_dict(ev))
        midi = ev_to_midi(ev)
        if midi:
            # logger.debug("ALSA to MIDI: %s", [hex(x) for x in midi])
            rtp_midi.send(midi)


def add_task(fn, **kwargs):
    task_queue.put((fn, kwargs))
    task_ready_fds[1].write(b"1")
    task_ready_fds[1].flush()
    # logger.debug("Add task to queue")


def process_tasks(fd):
    fd.read(1024)  # clear queue
    while not task_queue.empty():
        (fn, kwargs) = task_queue.get()
        try:
            fn(**kwargs)
        except Exception as e:
            logger.error("Error executing task: %s", e)
            traceback.print_exc()


class AppleMidiListener:
    def remove_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        if info:
            logger.info("Service removed: %s, %s, %s", repr(info.get_name()), [x for x in info.address], info.port)
        else:
            logger.info("Service removed: %s, %s, %s, %s" % (self, zeroconf, type, name))

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        if not info:
            logger.error("Got service from zeroconf, but no info. name: %s", name)
        logger.info("Service added: %s, %s, %s", repr(info.get_name()), [x for x in info.address], info.port)
        # logger.debug("No autoconnect.")
        add_task(add_applemidi, address=info.address, port=info.port)


def add_applemidi(address, port):
    rtp_midi.connect_to('.'.join(str(x) for x in address), port)


class RTPMidi:
    def __init__(self, local_port=PORT):
        """
        Opens an RTP connection to that port.
        """
        self.peers = {}
        self.initiators = {}
        self.control_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.control_sock.bind(('0.0.0.0', local_port))
        self.midi_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.midi_sock.bind(('0.0.0.0', local_port + 1))

    def filenos(self):
        return (self.control_sock.fileno(), self.midi_sock.fileno())

    def data_ready(self, fd):
        # logger.debug(
        #     "Data ready fd: %d, midi: %d, control: %d",
        #     fd, self.midi_sock.fileno(), self.control_sock.fileno()
        # )
        if fd == self.midi_sock.fileno():
            self.process_midi()
        elif fd == self.control_sock.fileno():
            self.process_control()
        else:
            logger.error(
                "Dont know who to send data fd: %d, midi: %d, control: %d",
                fd, self.midi_sock.fileno(), self.control_sock.fileno()
            )

    def connect_to(self, hostname, port):
        port = int(port)
        initiator_id = random.randint(0, 0xFFFFFFFF)
        peer = RTPConnection(self, hostname, port, initiator_id=initiator_id, client=True)
        logger.debug("Connect with initiator: %X", initiator_id)
        self.initiators[initiator_id] = peer

    def accept_connection(self, hostname, port, msg):
        logger.debug("Accept connection: %s", to_hex_str(msg))
        (_signature, _command, _protocol, initiator, remote_id) = struct.unpack("!HHLLL", msg[:16])
        name = ''
        for x in msg[16:]:
            if x == '\0':
                break
            name += chr(x)
        logger.debug("Remote name is: %s", name)

        peer = RTPConnection(self, hostname, port, initiator_id=initiator, remote_id=remote_id, client=False, name=name)
        peer.sync()
        self.initiators[initiator] = peer
        self.peers[remote_id] = peer
        return (remote_id, b'')

    def initiator_is_peer(self, initiator, peer):
        """
        We know control and midi from an internal random id, but receive some
        messages from the remote SSRC. Do the mapping.

        Both at the same mapping, expecting no collissions.
        """
        logger.debug("Initiator: %X is peer: %X", initiator, peer)

        self.peers[peer] = self.initiators[initiator]

    def process_midi(self):
        (source, msg) = self.remote_data_read(self.midi_sock)
        for ev in midi_to_evs(msg):
            peer = self.peers.get(source)
            if peer and ev:
                    logger.debug("Network MIDI from: %s event: %s", peer.name, ev_to_dict(ev))
                    alsaseq.output(ev)
            elif not peer:
                logger.warn("Unknown source, ignoring. TODO: Send disconnect.")

    def process_control(self):
        (source, msg) = self.remote_data_read(self.control_sock)
        # logger.debug("Done with control from %s: %s", source and hex(source), to_hex_str(msg))

    def rtp_decode(self, msg):
        (flags, type, sequence_nr, timestamp, source) = struct.unpack("!BBHLL", msg)

        return (flags, type, sequence_nr, timestamp, source)

    def remote_data_read(self, sock):
        (msg, from_) = sock.recvfrom(1500)
        # logger.debug("Got data at from: %s, msg: %s", from_, to_hex_str(msg))

        is_command = struct.unpack("!H", msg[:2])[0] == 0xFFFF
        if is_command:
            command = struct.unpack("!H", msg[2:4])[0]

            # connecting to me, how exciting!
            if command == RTPConnection.Commands.IN:
                initiator = struct.unpack("!L", msg[8:12])[0]
                peer = self.initiators.get(initiator)
                if peer:
                    peer.accept_connect(self.midi_sock, from_[1])
                    return (initiator, b'')
                return self.accept_connection(*from_, msg)

            elif command == RTPConnection.Commands.OK:
                initiator = struct.unpack("!L", msg[8:12])[0]
                peer = self.initiators.get(initiator)
                if not peer:
                    logger.error(
                        "Bad OK from from unknown initiator: %X. msg: %s",
                        initiator,
                        to_hex_str(msg)
                    )
                    return (initiator, b'')
                return peer.accepted_connection(from_, msg)

            elif command == RTPConnection.Commands.CK:
                remote_id = struct.unpack("!L", msg[4:8])[0]
                peer = self.peers.get(remote_id)
                if not peer:
                    logger.error(
                        "Bad CK from peer: %X. msg: %s",
                        remote_id,
                        to_hex_str(msg)
                    )
                    return (remote_id, b'')

                return peer.recv_sync(msg)

            elif command == RTPConnection.Commands.BY:
                initiator = struct.unpack("!L", msg[8:12])[0]
                peer = self.initiators.get(initiator)
                if not peer:
                    logger.error(
                        "Bad BY by initiator: %X. msg: %s",
                        initiator,
                        to_hex_str(msg)
                    )
                    return (initiator, b'')

                logger.info("Closed connection with remote: %X, initiator: %X" % (peer.remote_id, initiator))
                if peer.remote_id:
                    del self.peers[peer.remote_id]
                del self.initiators[initiator]
                return (initiator, b'')

            elif command == RTPConnection.Commands.RS:
                (peer_id, timestamp) = struct.unpack("!LL", msg[4:12])
                peer = self.peers.get(peer_id)
                if not peer:
                    logger.error(
                        "Bad RS by peer: %X. msg: %s",
                        initiator,
                        to_hex_str(msg)
                    )
                    return (initiator, b'')

                logger.warn("No journal to resent events yet. RS ACK peer: %s, timestamp: %s" % (
                    peer.remote_id, timestamp))
                return (peer_id, b'')

            logger.error(
                "Unimplemented command: %s%s."
                "Maybe RTP message (reuse of connection). Maybe MIDI command.",
                chr(command >> 8), chr(command & 0x0FF)
            )
            return (None, b'')

        rtp = self.rtp_decode(msg[:12])
        source = rtp[4]
        return (source, msg[13:])

    def send(self, msg):
        for peer in self.peers.values():
            peer.send_midi(msg)

    def close_all(self):
        for p in self.peers.values():
            p.close()


class RTPConnection:
    class State:
        NOT_CONNECTED = 0
        SENT_REQUEST = 1
        CONNECTED = 2
        SYNC = 3
        CLOSED = 4

    class Commands:
        IN = 0x494e  # Just the chars
        OK = 0x4f4b
        NO = 0x4e4f
        BY = 0x4259
        CK = 0x434b
        RS = 0x5253

    def __init__(self, rtpmidi, remote_host, remote_port, initiator_id, client, name=None, remote_id=False):
        self.remote_host = remote_host
        self.remote_port = remote_port
        self.name = name
        self.initiator_id = initiator_id
        self.rtpmidi = rtpmidi
        self.conn_start = time.time()
        self.remote_id = remote_id
        self.seq1 = random.randint(0, 0xFFFF)
        self.seq2 = random.randint(0, 0xFFFF)
        self.connect_timeout = {}
        if client:
            self.state = RTPConnection.State.NOT_CONNECTED
            self.connect(rtpmidi.control_sock, self.remote_port)
            self.connect(rtpmidi.midi_sock, self.remote_port+1)
        else:
            self.accept_connect(rtpmidi.control_sock, self.remote_port)
            self.state = RTPConnection.State.CONNECTED

    def connect(self, sock, port):
        signature = 0xFFFF
        command = RTPConnection.Commands.IN
        protocol = 2
        sender = SSRC

        msg = (struct.pack("!HHLLL", signature, command, protocol, self.initiator_id, sender)
               + NAME.encode('utf8')
               + b'\0')
        sock.sendto(msg, (self.remote_host, port))
        logger.info("[%X] Connect to %s:%d: %s", self.initiator_id, self.remote_host, port, to_hex_str(msg))
        self.state = RTPConnection.State.SENT_REQUEST
        self.connect_timeout[port] = event_dispatcher.call_later(
            30, self.connect, sock, port
        )

    def accepted_connection(self, from_, msg):
        (protocol, initiator_id, sender) = struct.unpack("!LLL", msg[4:16])
        name = ""
        for m in msg[16:]:
            if m == b'\0':
                break
            name += chr(m)
        assert self.initiator_id == initiator_id, "Got wrong message: " + to_hex_str(msg)
        logger.info(
            "[%X] Connected local_port host: %s:%d, name: %s, remote_id: %X",
            self.initiator_id, self.remote_host, self.remote_port, repr(name), sender
        )
        self.name = name
        self.state = RTPConnection.State.CONNECTED
        self.rtpmidi.initiator_is_peer(self.initiator_id, sender)
        self.remote_id = sender

        (_addr, port) = from_
        event_dispatcher.remove_call_later(self.connect_timeout[port])
        del self.connect_timeout[port]

        return (None, [])

    def accept_connect(self, sock, port):
        signature = 0xFFFF
        command = RTPConnection.Commands.OK
        protocol = 2
        sender = SSRC

        msg = (struct.pack("!HHLLL", signature, command, protocol, self.initiator_id, sender)
               + NAME.encode('utf8')
               + b'\0')
        sock.sendto(msg, (self.remote_host, port))
        logger.info("[%X] Accept connect to %s:%d: %s", self.initiator_id, self.remote_host, port, to_hex_str(msg))
        self.state = RTPConnection.State.SENT_REQUEST

    def close(self):
        signature = 0xFFFF
        command = RTPConnection.Commands.BY
        protocol = 2
        sender = SSRC

        msg = struct.pack("!HHLLL", signature, command, protocol, self.initiator_id, sender)
        self.rtpmidi.control_sock.sendto(msg, (self.remote_host, self.remote_port))
        self.rtpmidi.midi_sock.sendto(msg, (self.remote_host, self.remote_port+1))
        logger.info(
            "[%X] Close connection %s:%d: %s", self.initiator_id, self.remote_host, self.remote_port, to_hex_str(msg)
        )
        self.state = RTPConnection.State.CLOSED

    def sync(self):
        self.state = RTPConnection.State.SYNC
        logger.debug("[%X] Sync", self.initiator_id)

        t1 = int(self.time() * 10000)  # current time or something in 100us units
        msg = struct.pack(
            "!HHLbbHQQQ",
            0xFFFF, RTPConnection.Commands.CK, self.initiator_id, 0, 0, 0,
            t1, 0, 0
        )
        self.rtpmidi.midi_sock.sendto(msg, (self.remote_host, self.remote_port))

    def recv_sync(self, msg):
        (sender, count, _, _, t1, t2, t3) = struct.unpack("!LbbHQQQ", msg[4:])
        if count == 0:
            self.sync1(sender, t1)
        if count == 1:
            self.sync2(sender, t1, t2)
        if count == 2:
            self.sync3(sender, t1, t2, t3)
        return (sender, b'')

    def sync1(self, sender, t1):
        # logger.debug("[%X] Sync1", sender)
        t2 = int(self.time() * 10000)  # current time or something in 100us units
        msg = struct.pack(
            "!HHLbbHQQQ",
            0xFFFF, RTPConnection.Commands.CK, SSRC, 1, 0, 0,
            t1, t2, 0
        )
        self.rtpmidi.midi_sock.sendto(msg, (self.remote_host, self.remote_port + 1))

    def sync2(self, sender, t1, t2):
        # logger.debug("[%X] Sync2", sender)
        t3 = int(self.time() * 10000)  # current time or something in 100us units (10ths of ms)
        self.offset = ((t1 + t3) / 2) - t2
        logger.info(
            "[%X] Sync2 offset is now: %d for: %X (%s). latency: %.2fms",
            self.initiator_id, self.offset, sender, self.name,
            (t3-t1) / 20.0
        )
        msg = struct.pack(
            "!HHLbbHQQQ",
            0xFFFF, RTPConnection.Commands.CK, SSRC, 2, 0, 0,
            t1, t2, t3
        )
        self.rtpmidi.midi_sock.sendto(msg, (self.remote_host, self.remote_port + 1))

    def sync3(self, sender, t1, t2, t3):
        # logger.debug("[%X] Sync3", sender)
        self.offset = ((t1 + t3) / 2) - t2
        logger.info(
            "[%X] Sync3 offset is now: %d for: %X (%s). latency: %.2fms",
            self.initiator_id, self.offset, sender, self.name,
            (t3-t1) / 20.0
        )

    def time(self):
        return time.time() - self.conn_start

    def send_midi(self, msg):
        if not self.conn_start:
            # Not yet connected
            return
        if len(msg) > 16:
            raise Exception("Current implementation max event size is 16 bytes")
        # Short header, no fourname, no deltatime, status in first byte, 4 * length. So just length.
        rtpmidi_header = [len(msg)]
        timestamp = int(self.time() * 1000)
        self.seq1 += 1
        self.seq2 += 1

        rtpheader = [
            0x80, 0x61,
            byten(self.seq1, 1), byten(self.seq1, 0),
            # byten(self.seq2, 1), byten(self.seq2, 0),  # sequence nr
            byten(timestamp, 3), byten(timestamp, 2), byten(timestamp, 1), byten(timestamp, 0),
            byten(SSRC, 3), byten(SSRC, 2),
            byten(SSRC, 1), byten(SSRC, 0),  # sequence nr
        ]
        msg = bytes(rtpheader) + bytes(rtpmidi_header) + bytes(msg)

        self.rtpmidi.midi_sock.sendto(msg, (self.remote_host, self.remote_port + 1))

        # logger.debug(self.name, ' '.join(["%2X" % x for x in msg]))

    def __str__(self):
        return "[%X] %s" % (self.initiator_id, self.name)


def to_hex_str(msg):
    s = ""
    for n, x in enumerate(msg):
        s += '{0:02X}'.format(x)
        if (n % 4) == 3:
            s += "  "
        else:
            s += " "
    return s


def byten(nr, n):
    return (nr >> (8*n)) & 0x0FF


def ev_to_dict(ev):
    """
    Converts an event to a struct. Mainly for debugging.
    """
    (type, flags, tag, queue, timestamp, source, destination, data) = ev
    return {
        "type": hex(type),
        "flags": flags,
        "tag": tag,
        "queue": queue,
        "timestamp": timestamp,
        "source": source,
        "destination": destination,
        "data": data,
    }


def midi_to_evs(source):
    current = 0
    data = []
    evlen = 2
    for c in source:
        if c & 0x080:
            current = c
            data = [current]
            if current in [0xC0, 0xD0]:
                evlen = 1
            else:
                evlen = 2
        elif current != 0xF0 and len(data) == evlen:
            data.append(c)
            yield midi_to_ev(data)
            data = [current]
        elif current == 0xF0 and c == 0x7F:
            yield midi_to_ev(data)
            data = ""
        else:
            data.append(c)

    if len(data) == 2:
        yield midi_to_ev(data)


# check names at https://github.com/Distrotech/alsa-lib/blob/distrotech-alsa-lib/include/seq_event.h
MIDI_TO_EV = {
    0x80: alsaseq.SND_SEQ_EVENT_NOTEOFF,  # note off
    0x90: alsaseq.SND_SEQ_EVENT_NOTEON,  # note on
    0xA0: alsaseq.SND_SEQ_EVENT_KEYPRESS,  # Poly key pressure / After touch
    0xB0: alsaseq.SND_SEQ_EVENT_CONTROLLER,  # CC
    # 0xC0: alsaseq.SND_SEQ_EVENT_PGMCHANGE,   # Program change / 1b
    # 0xD0: alsaseq.SND_SEQ_EVENT_CHANPRESS,   # Channel key pres / 1b
    0xE0: alsaseq.SND_SEQ_EVENT_PITCHBEND,  # pitch bend
}
EV_TO_MIDI = dict({v: k for k, v in MIDI_TO_EV.items()})  # just the revers


def midi_to_ev(event):
    type = MIDI_TO_EV.get(event[0] & 0x0F0)
    if not type:
        print("Unknown MIDI Event %s" % to_hex_str(event))
        return None

    len_event = len(event)

    if type == alsaseq.SND_SEQ_EVENT_PITCHBEND:
        _, lsb, msb = event
        return (
            type,
            0,
            0,
            253,
            (0, 0),
            (0, 0),
            (0, 0),

            (0, 0, 0, 0, 0, (msb << 7) + lsb)
        )
    elif type in (alsaseq.SND_SEQ_EVENT_NOTEON, alsaseq.SND_SEQ_EVENT_NOTEOFF) and len_event == 3:
        _, param1, param2 = event
        return (
            type,
            0,
            0,
            253,
            (0, 0),
            (0, 0),
            (0, 0),
            (0, param1, param2, 0, 0)
        )
    elif type == alsaseq.SND_SEQ_EVENT_CONTROLLER and len_event == 3:
        _, param1, param2 = event
        return (
            type,
            0,
            0,
            253,
            (0, 0),
            (0, 0),
            (0, 0),
            (0, 0, 0, 0, param1, param2)
        )
    logger.warn("Unimplemented MIDI event: %s, type: %s", event[0], type)
    return None


def ev_to_midi(ev):
    if ev[0] in (alsaseq.SND_SEQ_EVENT_NOTEON, alsaseq.SND_SEQ_EVENT_NOTEOFF):
        return (EV_TO_MIDI[ev[0]], ev[7][1], ev[7][2])
    if ev[0] == alsaseq.SND_SEQ_EVENT_CONTROLLER:
        return (EV_TO_MIDI[ev[0]], ev[7][4], ev[7][5])
    if ev[0] == alsaseq.SND_SEQ_EVENT_PITCHBEND:
        n = ev[7][5]
        return (EV_TO_MIDI[ev[0]], (n >> 7) & 0x07F, n & 0x07F)
    if ev[0] == 66:
        logger.info("New connection.")
        return None
    logger.warn("Unimplemented ALSA event: %d" % ev[0])
    return None


def maybe_wrap(maybelist):
    """
    Always returns an iterable. If None an empty one, if one element, a tuple with
    one element. If a list or tuple itself.
    """
    if maybelist is None:
        return ()
    if isinstance(maybelist, (list, tuple)):
        return maybelist
    return (maybelist,)


def test():
    events = list(midi_to_evs([0x90, 10, 10, 0x80, 10, 0]))
    print(events)


if len(sys.argv) == 2 and sys.argv[1] == "test":
    test()
else:
    main()
