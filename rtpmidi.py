#!env/bin/python3
import alsaseq
import sys
import time
import socket
import struct
import random
import select


SSRC = random.randint(0, 0xffffffff)  # Not sure if should be fixed by client type  0x17026324  # random ID
NAME = '%s - ALSA SEQ' % (socket.gethostname)
PORT = 10008

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
        print("Ready %s" % n, end="\r")
        for (fd, event) in epoll.poll():
            if fd == alsa_fd:
                process_alsa()
            elif fd in rtp_conn.filenos():
                rtp_conn.data_ready(fd)
        n += 1


def process_alsa():
    alsaseq.inputpending()
    ev = alsaseq.input()
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
            # print("Message from %s (%d): " % (remote_hosts.get(source), source), end="")
            if ev:
                print("Network MIDI: ", ev_to_dict(ev))
                alsaseq.output(ev)
            else:
                print("Unknown")

    def process_control(self):
        (source, msg) = self.sock_control.remote_data_read()
        print("Got control from %s" % source, to_hex_str(msg))


class RTPConnection:
    class State:
        NOT_CONNECTED = 0
        SENT_REQUEST = 1
        CONNECTED = 2
        SYNC1 = 3
        SYNC2 = 4
        SYNC3 = 5

    def __init__(self, local_port, remote_host, remote_port):
        self.local_port = local_port
        self.remote_host = remote_host
        self.remote_port = remote_port
        self.state = self.State.NOT_CONNECTED
        self.remote_name = None
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('0.0.0.0', self.local_port))

        self.connect()

    def fileno(self):
        return self.sock and self.sock.fileno()

    def connect(self):
        signature = 0xFFFF  # ??
        command = 0x494e
        protocol = 2
        initiator = random.randint(0, 0xffffffff)
        sender = SSRC
        self.initiator = initiator

        # print("initiator id", initiator)

        msg = struct.pack("!HHLLL", signature, command, protocol, initiator, sender) + NAME.encode('utf8') + b'\0'
        self.sock.sendto(msg, (self.remote_host, self.remote_port))
        self.state = self.State.SENT_REQUEST

    def remote_data_read(self):
        (msg, from_) = self.sock.recvfrom(1500)

        if self.state == self.State.SENT_REQUEST:
            (signature, command, protocol, initiator2, sender) = struct.unpack("!HHLLL", msg[:16])
            name = ""
            for m in msg[16:]:
                if m == b'\0':
                    break
                name += chr(m)
            assert self.initiator == initiator2, "Got wrong message"
            print("Connected ", self.local_port, " to ", self.remote_port, " name ", repr(name))
            remote_hosts[initiator2] = name
            self.remote_name = name
            self.state = self.State.CONNECTED
            return (None, [])
        else:
            rtp = self.rtp_decode(msg[:12])
            source = rtp[4]
            return (source, msg[13:])

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
    # 0xA0: alsaseq.SND_SEQ_EVENT_KEYPRESS,  # Poly key pressure / After touch
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
    return None


def test():
    events = list(midi_to_alsaevents([0x90, 10, 10, 0x80, 10, 0]))
    print(events)


if len(sys.argv) == 2 and sys.argv[1] == "test":
    test()
else:
    main()
