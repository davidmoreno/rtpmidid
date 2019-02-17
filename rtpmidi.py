#!env/bin/python3
import alsaseq
import sys
import time
import socket
import struct
import random
import select


SSRC = random.randint(0, 0xffffffff)  # Not sure if should be fixed by client type  0x17026324  # random ID
NAME = '%s - ALSA SEQ' % (socket.gethostname())
PORT = 10008
DEBUG = False

remote_hosts = {}


def main():
    hostport = sys.argv[1].split(':')
    epoll = select.epoll()

    print("RTP/MIDI v.0")
    alsaseq.client("Network", 1, 1, False)

    rtp_conn = RTPMidiClient(PORT, hostport[0], int(hostport[1]))
    alsa_fd = alsaseq.fd()
    (control_fd, midi_fd) = rtp_conn.filenos()

    epoll.register(alsa_fd, select.EPOLLIN | select.EPOLLHUP | select.EPOLLERR)
    epoll.register(control_fd, select.EPOLLIN)
    epoll.register(midi_fd, select.EPOLLIN)

    print("Loop")
    n = 0
    while True:
        print("Event count: %s" % n, end="\r")
        for (fd, event) in epoll.poll():
            if fd == alsa_fd:
                process_alsa()
            elif fd in rtp_conn.filenos():
                rtp_conn.data_ready(fd)
        n += 1


def process_alsa():
    alsaseq.inputpending()
    ev = alsaseq.input()
    if DEBUG:
        print("ALSA: ", ev_to_dict(ev))


class RTPMidiClient:
    def __init__(self, local_port, remote_host, remote_port):
        """
        Opens an RTP connection to that port.
        """
        self.sock_control = RTPConnection(local_port, remote_host, remote_port)
        self.sock_midi = RTPConnection(local_port + 1, remote_host, remote_port + 1)

    def filenos(self):
        return (self.sock_control.fileno(), self.sock_midi.fileno())

    def data_ready(self, fd):
        if fd == self.sock_midi.fileno():
            self.process_midi()
        elif fd == self.sock_control.fileno():
            self.process_control()

    def process_midi(self):
        (source, msg) = self.sock_midi.remote_data_read()
        for ev in midi_to_alsaevents(msg):
            sname = remote_hosts.get(source)
            print("Message from %s (%d): " % (sname, source), end="")
            if sname:
                if ev:
                    if DEBUG:
                        print("Network MIDI: ", ev_to_dict(ev))
                    alsaseq.output(ev)
            else:
                print("Unknown source, ignoring.")

    def process_control(self):
        (source, msg) = self.sock_control.remote_data_read()
        print("Got control from %s" % source, to_hex_str(msg))


class RTPConnection:
    class State:
        NOT_CONNECTED = 0
        SENT_REQUEST = 1
        CONNECTED = 2
        SYNC = 3

    class Commands:
        IN = 0x494e  # Just the chars
        OK = 0x4f4b
        NO = 0x4e4f
        BY = 0x4259
        CK = 0x434b

    def __init__(self, local_port, remote_host, remote_port):
        self.local_port = local_port
        self.remote_host = remote_host
        self.remote_port = remote_port
        self.state = RTPConnection.State.NOT_CONNECTED
        self.remote_name = None
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('0.0.0.0', self.local_port))
        self.initiator = random.randint(0, 0xffffffff)

        self.connect()

    def fileno(self):
        return self.sock and self.sock.fileno()

    def connect(self):
        signature = 0xFFFF  # ??
        command = RTPConnection.Commands.IN
        protocol = 2
        sender = SSRC

        # print("initiator id", initiator)

        msg = struct.pack("!HHLLL", signature, command, protocol, self.initiator, sender) + NAME.encode('utf8') + b'\0'
        self.sock.sendto(msg, (self.remote_host, self.remote_port))
        self.state = RTPConnection.State.SENT_REQUEST

    def remote_data_read(self):
        (msg, from_) = self.sock.recvfrom(1500)

        is_command = struct.unpack("!H", msg[:2])[0] == 0xFFFF
        if is_command:
            command = struct.unpack("!H", msg[2:4])[0]
            if command == RTPConnection.Commands.OK and self.state == RTPConnection.State.SENT_REQUEST:
                return self.accepted_connection(msg)
            elif command == RTPConnection.Commands.CK:
                self.recv_sync(msg)
            else:
                print(
                    "Unimplemented command %X. Maybe RTP message (reuse of connection). Maybe MIDI command." % command
                )

        self.state = RTPConnection.State.CONNECTED
        rtp = self.rtp_decode(msg[:12])
        source = rtp[4]
        return (source, msg[13:])

    def sync(self):
        self.state = RTPConnection.State.SYNC

        t1 = int(time.time() * 10000)  # current time or something in 100us units
        msg = struct.pack(
            "!HHLbbHQQQ",
            0xFFFF, RTPConnection.Commands.CK, self.initiator, 0, 0, 0,
            t1, 0, 0
        )
        self.sock.sendto(msg, (self.remote_host, self.remote_port))

    def recv_sync(self, msg):
        print("Sync: ", to_hex_str(msg))
        (sender, count, _, _, t1, t2, t3) = struct.unpack("!LbbHQQQ", msg[4:])
        print(sender, count, t1, t2, t3)
        if count == 0:
            self.sync1(sender, t1)
        if count == 1:
            self.sync2(sender, t1, t2)
        if count == 2:
            self.sync3(sender, t1, t2, t3)

    def sync1(self, sender, t1):
        t2 = int(time.time() * 10000)  # current time or something in 100us units
        msg = struct.pack(
            "!HHLbbHQQQ",
            0xFFFF, RTPConnection.Commands.CK, self.initiator, 1, 0, 0,
            t1, t2, 0
        )
        self.sock.sendto(msg, (self.remote_host, self.remote_port))

    def sync2(self, sender, t1, t2):
        t3 = int(time.time() * 10000)  # current time or something in 100us units
        self.offset = ((t1 + t3) / 2) - t2
        print("Offset is now: %d for: %d " % (self.offset, sender))
        msg = struct.pack(
            "!HHLbbHQQQ",
            0xFFFF, RTPConnection.Commands.CK, self.initiator, 2, 0, 0,
            t1, t2, t3
        )
        self.sock.sendto(msg, (self.remote_host, self.remote_port))

    def sync3(self, sender, t1, t2, t3):
        self.offset = ((t1 + t3) / 2) - t2
        print("Offset is now: %d for: %d " % (self.offset, sender))

    def accepted_connection(self, msg):
        (protocol, rtp_id, sender) = struct.unpack("!LLL", msg[4:16])
        name = ""
        for m in msg[16:]:
            if m == b'\0':
                break
            name += chr(m)
        assert self.initiator == rtp_id, "Got wrong message: " + to_hex_str(msg)
        print("Connected ", self.local_port, " to ", self.remote_port, " name ", repr(name), " remote id ", sender)
        remote_hosts[sender] = name
        self.remote_name = name
        self.state = RTPConnection.State.CONNECTED
        self.sync()
        return (None, [])

    def rtp_decode(self, msg):
        (flags, type, sequence_nr, timestamp, source) = struct.unpack("!BBHLL", msg)

        return (flags, type, sequence_nr, timestamp, source)


def print_hex(msg):
    print(to_hex_str(msg))


def to_hex_str(msg):
    return " ".join(hex(x) for x in msg)


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


def midi_to_alsaevents(source):
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
            yield midi_to_alsaevent(data)
            data = [current]
        elif current == 0xF0 and c == 0x7F:
            yield midi_to_alsaevent(data)
            data = ""
        else:
            data.append(c)


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


def midi_to_alsaevent(event):
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
    print("Unimplemented MIDI event", event[0], type)
    return None


def test():
    events = list(midi_to_alsaevents([0x90, 10, 10, 0x80, 10, 0]))
    print(events)


if len(sys.argv) == 2 and sys.argv[1] == "test":
    test()
else:
    main()
