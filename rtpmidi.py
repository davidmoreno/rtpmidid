#!env/bin/python3
import alsaseq
import sys
import time
import socket
import struct
import random
import select


SSRC = 0x17026324  # random ID
NAME = 'ucube'
PORT = 10008

remote_hosts = {}


def main():
    hostport = sys.argv[1].split(':')
    control = (hostport[0], int(hostport[1]))
    midi = (hostport[0], int(hostport[1])+1)
    epoll = select.epoll()

    print("RTP/MIDI v.0")
    alsaseq.client("Network", 1, 1, False)
    (control, _) = connect_to(PORT, control)
    (midi, _) = connect_to(PORT + 1, midi)

    alsa_fd = alsaseq.fd()
    control_fd = control.fileno()
    midi_fd = midi.fileno()

    epoll.register(alsa_fd, select.EPOLLIN | select.EPOLLHUP | select.EPOLLERR)
    epoll.register(control_fd, select.EPOLLIN)
    epoll.register(midi_fd, select.EPOLLIN)

    print(remote_hosts)

    print("Loop")
    n = 0
    while True:
        print("Ready %s" % n, end="\r")
        for (fd, event) in epoll.poll():
            if fd == alsa_fd:
                process_alsa()
            elif fd == control_fd:
                process_control(control)
            elif fd == midi_fd:
                process_midi(midi)
        n += 1


def process_alsa():
    alsaseq.inputpending()
    ev = alsaseq.input()
    print("ALSA: ", ev_to_dict(ev))


def process_midi(midi):
    for (source, ev) in recv_midi(midi):
        # print("Message from %s (%d): " % (remote_hosts.get(source), source), end="")
        if ev:
            print("Network MIDI: ", ev_to_dict(ev))
            alsaseq.output(ev)
        else:
            print("Unknown")


def process_control(control):
    (msg, from_) = control.recvfrom(1500)
    print("Got control from %s" % from_, end=": ")
    print_hex(msg)


def connect_to(from_port, hostport):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', from_port))

    signature = 0xFFFF  # ??
    command = 0x494e
    protocol = 2
    initiator = random.randint(0, 0xffffffff)
    sender = SSRC

    # print("initiator id", initiator)

    msg = struct.pack("!HHLLL", signature, command, protocol, initiator, sender) + NAME.encode('utf8') + b'\0'
    # print("Send: ", msg)
    # print_hex(msg)

    sock.sendto(msg, hostport)
    (msg, from_) = sock.recvfrom(1500)
    # print("Recv: ", msg)
    # print_hex(msg)

    (signature, command, protocol, initiator2, sender) = struct.unpack("!HHLLL", msg[:16])
    name = ""
    for m in msg[16:]:
        if m == b'\0':
            break
        name += chr(m)
    assert initiator == initiator2, "Got wrong message"
    print("Connected ", from_port, " to ", hostport, " name ", repr(name))

    remote_hosts[initiator2] = name
    return (sock, name)


def recv_midi(sock):
    (msg, from_) = sock.recvfrom(1500)
    rtp = rtp_decode(msg[:12])
    source = rtp[4]
    # rtp_midi = rtp_midi_decode(msg[12])

    for event in midi_to_alsaevents(msg[13:]):
        yield (source, event)


def rtp_decode(msg):
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


MIDI_TO_EV = {
    0x80: 0x7,  # note off
    0x90: 0x6,  # note on
    # 0xA0: XX,  # Poly key pressure
    # 0xB0: XX,  # CC
    # 0xC0: X,   # Program change
    # 0xD0: X,   # Channel key pres
    0xE0: 0xd,  # pitch bend
}


def midi_to_alsaevent(event):
    type = MIDI_TO_EV.get(event[0] & 0x0F0)
    if not type:
        print("Unknown MIDI Event %s" % to_hex_str(event))
        return None

    if type == 0xd:
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

    if len(event) == 3:
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
    return None


def test():
    events = list(midi_to_alsaevents([0x90, 10, 10, 0x80, 10, 0]))
    print(events)


if len(sys.argv) == 2 and sys.argv[1] == "test":
    test()
else:
    main()
