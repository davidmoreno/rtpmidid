#!/usr/bin/python3

import socket
import sys
import json
import os
import time


class Connection:
    def __init__(self, filename):
        self.filename = filename
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            self.socket.connect(filename)
        except ConnectionRefusedError:
            print(
                "Connection refused to %s. Can set other location with --control=<path>"
                % filename
            )
            sys.exit(1)

    def command(self, command):
        self.socket.send(json.dumps(command).encode("utf8") + b"\n")
        data = self.socket.recv(1024 * 1024)  # load all you can. 1MB cool!
        return json.loads(data)


def main(argv):
    settings = parse_arguments(argv)
    if settings.control:
        socketpath = settings.control
    else:
        socketpath = "/var/run/rtpmidid/control.sock"

    if not os.path.exists(socketpath):
        print("Control socket %s does not exist" % socketpath)
        sys.exit(1)

    try:
        conn = Connection(socketpath)
    except Exception as e:
        print(str(e))
        sys.exit(1)

    if settings.top:
        return top_loop(conn)

    for cmd in parse_commands(settings.command or ["help"]):
        print(">>> %s" % json.dumps(cmd), file=sys.stderr)
        ret = conn.command(cmd)
        print(json.dumps(ret, indent=2))


def maybe_int(txt: str):
    try:
        return int(txt)
    except:
        pass
    return txt


def parse_arguments(argv):
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--control")
    parser.add_argument("--top", action="store_true")
    parser.add_argument("command", nargs="*")

    return parser.parse_args(argv[1:])


def parse_commands(argv):
    cmd = []
    for x in argv:
        if x == ".":
            yield {"method": cmd[0], "params": [maybe_int(x) for x in cmd[1:]]}
            cmd = []
        else:
            cmd.append(x)
    if cmd:
        yield {"method": cmd[0], "params": [maybe_int(x) for x in cmd[1:]]}


ANSI_CLEAR_SCREEN = "\033[2J\033[H"
ANSI_PUSH_SCREEN = "\033[?1049h"
ANSI_POP_SCREEN = "\033[?1049l"

ANSI_TEXT_BLUE = "\033[34m"
ANSI_TEXT_RED = "\033[31m"
ANSI_TEXT_GREEN = "\033[32m"
ANSI_TEXT_YELLOW = "\033[33m"
ANSI_TEXT_PURPLE = "\033[35m"
ANSI_TEXT_CYAN = "\033[36m"
ANSI_TEXT_WHITE = "\033[37m"
ANSI_TEXT_BLACK = "\033[30m"
ANSI_TEXT_GREY = "\033[90m"
ANSI_TEXT_BOLD = "\033[1m"

ANSI_BG_BLUE = "\033[44m"
ANSI_BG_RED = "\033[41m"
ANSI_BG_GREEN = "\033[42m"
ANSI_BG_YELLOW = "\033[43m"
ANSI_BG_PURPLE = "\033[45m"
ANSI_BG_CYAN = "\033[46m"
ANSI_BG_WHITE = "\033[47m"
ANSI_BG_BLACK = "\033[40m"
ANSI_BG_GREY = "\033[100m"

ANSI_RESET = "\033[0m"


def safe_getter(*keys):
    def getter(data):
        try:
            for key in keys:
                if key in data:
                    data = data[key]
                else:
                    return ""
        except:
            return ""
        return data

    return getter


def latency_getter(data):
    avg = safe_getter("peer", "latency_ms", "average")(data)
    if not avg:
        return ""
    stddev = safe_getter("peer", "latency_ms", "stddev")(data)
    return f"{avg}ms \u00B1 {stddev}ms"


COLUMNS = [
    {
        "name": "ID",
        "get": safe_getter("id"),
        "width": 8,
        "align": "right",
    },
    {
        "name": "Type",
        "get": safe_getter("type"),
        "width": 40,
        "align": "left",
    },
    {
        "name": "Name",
        "get": safe_getter("name"),
        "width": 60,
        "align": "left",
    },
    {
        "name": "Status",
        "get": safe_getter("peer", "status"),
        "width": 20,
        "align": "left",
    },
    {
        "name": "Sent",
        "get": safe_getter("stats", "sent"),
        "width": 8,
        "align": "right",
    },
    {
        "name": "Received",
        "get": safe_getter("stats", "recv"),
        "width": 8,
        "align": "right",
    },
    {
        "name": "Latency",
        # the unicode symbol for +- is \u00B1
        "get": latency_getter,
        "width": 20,
        "align": "right",
    },
    {
        "name": "Send To",
        "get": lambda x: ", ".join(str(x) for x in safe_getter("send_to")(x)),
        "width": 10,
    },
]


def print_table(table: list[list[str]]):
    def format_cell(cell, column):
        if column.get("align") == "right":
            return "{:>{width}}".format(cell, width=column["width"])
        else:
            return "{:{width}}".format(cell, width=column["width"])

    for idx, row in enumerate(table):
        print(get_color_row(row, idx), end="")
        print(" | ".join(format_cell(x, column) for x, column in zip(row, COLUMNS)))
        print(ANSI_RESET, end="")


def get_color_row(row, idx):
    if idx == 0:
        return ANSI_BG_CYAN + ANSI_TEXT_WHITE + ANSI_TEXT_BOLD
    elif row[3] == "CONNECTED":
        return ANSI_BG_GREEN + ANSI_TEXT_BLACK
    elif row[3] == "CONNECTING":
        return ANSI_BG_YELLOW + ANSI_TEXT_BLACK
    else:
        return ANSI_BG_GREY + ANSI_TEXT_BLACK


def top_loop(conn: Connection):
    print(ANSI_PUSH_SCREEN, end="")
    try:
        while True:
            print(ANSI_CLEAR_SCREEN, end="")
            data = conn.command({"method": "status"})
            table = [[x["name"] for x in COLUMNS]]
            peers = data["result"]["router"]
            table.extend([[x["get"](peer) for x in COLUMNS] for peer in peers])
            print_table(table)
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        print(ANSI_POP_SCREEN, end="")


if __name__ == "__main__":
    main(sys.argv)
