#!env/bin/python3
import alsaseq
import sys
import time
import socket
import struct
import random


SSRC = 0x17026324  # random ID
NAME = 'ucube'
PORT = 10008

remote_hosts = {}


def main():

    hostport = sys.argv[1].split(':')
    control = (hostport[0], int(hostport[1]))
    midi = (hostport[0], int(hostport[1])+1)

    print("RTP/MIDI v.0")
    alsaseq.client("Network", 1, 1, False)
    (control, _) = connect_to(PORT, control)
    (midi, _) = connect_to(PORT + 1, midi)

    print(remote_hosts)

    while True:
        for (source, event) in recv_midi(midi):
            print("Message from %s (%d): " % (remote_hosts.get(source), source), end="")
            print_hex(event)

    while True:
        alsaseq.inputpending()
        ev = alsaseq.input()
        print(ev_to_dict(ev))


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

    event = bytearray([msg[13]])
    for b in msg[14:]:
        if b & 0x80:   # high byte marks new command in MIDI
            yield (source, event)
            event = bytearray()
        event.append(b)
    yield (source, event)


def rtp_decode(msg):
    (flags, type, sequence_nr, timestamp, source) = struct.unpack("!BBHLL", msg)

    return (flags, type, sequence_nr, timestamp, source)


def print_hex(msg):
    for m in msg:
        print(hex(m), " ", end="")
    print()


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


main()
