#!/usr/bin/python3

import socket
import sys
import json
import os


class Connection:
    def __init__(self, filename):
        self.filename = filename
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            self.socket.connect(filename)
        except ConnectionRefusedError:
            print("Connection refused to %s. Change control socket location with RTPMIDID_SOCKET environment var." % filename)
            raise

    def command(self, command):
        self.socket.send(json.dumps(command).encode('utf8')+b'\n')
        data = self.socket.recv(1024*1024)  # load all you can. 1MB cool!
        return json.loads(data)


socketpath = os.environ.get(
    "RTPMIDID_SOCKET") or "/var/run/rtpmidid/control.sock"


def main(argv):
    try:
        conn = Connection(socketpath)
    except Exception as e:
        print(str(e))
        sys.exit(1)

    for cmd in parse_commands(argv[1:] or ["help"]):
        print(">>> %s" % json.dumps(cmd), file=sys.stderr)
        ret = conn.command(cmd)
        print(json.dumps(ret, indent=2))


def parse_commands(argv):
    cmd = []
    for x in argv:
        if x == '.':
            yield {"method": cmd[0], "params": cmd[1:]}
            cmd = []
        else:
            cmd.append(x)
    if cmd:
        yield {"method": cmd[0], "params": cmd[1:]}


if __name__ == '__main__':
    main(sys.argv)
