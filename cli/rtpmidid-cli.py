#!/usr/bin/python3

import select
import tty
import shutil
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


def maybe_int(txt: str):
    try:
        return int(txt)
    except:
        pass
    return txt


def parse_arguments(argv):
    import argparse

    parser = argparse.ArgumentParser(
        usage="""rtpmidid-cli [options] [command]

rtpmidid-cli is a command line interface to rtpmidid. It can be used to
control rtpmidid and to monitor the status of the peers.

If no command is passed as argument, rtpmidid-cli will enter top mode.

In top mode, rtpmidid-cli will show a table with the status of the peers
and will allow to connect and disconnect peers. In top mode, the following
keys are available:

[h]            - show help                                    
[up] [down]    - navigate the table
[left] [right] - navigate the table columns
[q]            - quit

Common commands are:

help            - show all the remote available commands and some help
status          - show the status of the router
router.connect  - connect two peers
router.remove   - disconnect a peer
router.kill     - kill a peer
connect         - connects to a remote rtpmidi server

"""
    )
    parser.add_argument("--control")
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


def safe_get(data, *keys):
    try:
        for key in keys:
            if key in data:
                data = data[key]
            else:
                return ""
    except:
        return ""
    return data


class Top:
    ANSI_CLEAR_SCREEN = "\033[2J\033[H"
    ANSI_PUSH_SCREEN = "\033[?1049h"
    ANSI_POP_SCREEN = "\033[?1049l"

    ANSI_TEXT_BLACK = "\033[30m"
    ANSI_TEXT_RED = "\033[31m"
    ANSI_TEXT_GREEN = "\033[32m"
    ANSI_TEXT_YELLOW = "\033[33m"
    ANSI_TEXT_BLUE = "\033[34m"
    ANSI_TEXT_PURPLE = "\033[35m"
    ANSI_TEXT_CYAN = "\033[36m"
    ANSI_TEXT_WHITE = "\033[37m"
    ANSI_TEXT_GREY = "\033[90m"
    ANSI_TEXT_BOLD = "\033[1m"

    ANSI_BG_BLACK = "\033[40m"
    ANSI_BG_RED = "\033[41m"
    ANSI_BG_GREEN = "\033[42m"
    ANSI_BG_YELLOW = "\033[43m"
    ANSI_BG_BLUE = "\033[44m"
    ANSI_BG_PURPLE = "\033[45m"
    ANSI_BG_CYAN = "\033[46m"
    ANSI_BG_WHITE = "\033[47m"
    ANSI_BG_GREY = "\033[100m"

    ANSI_RESET = "\033[0m"

    def __init__(self, conn: Connection):
        # terminal width and height from stty
        self.conn = conn
        width, height = shutil.get_terminal_size()
        self.selected_row_index = 0
        self.selected_col_index = 0
        self.selected_row = None
        self.max_rows = height - 2
        self.width = width
        self.height = height
        self.status = {}

        self.COLUMNS = [
            {
                "name": "ID",
                "get": lambda data: safe_get(data, "id"),
                "width": 8,
                "align": "right",
            },
            {
                "name": "Type",
                "get": lambda data: safe_get(data, "type"),
                "width": 40,
                "align": "left",
            },
            {
                "name": "Name",
                "get": lambda data: safe_get(data, "name"),
                "width": 0,
                "align": "left",
            },
            {
                "name": "Status",
                "get": self.get_peer_status,
                "width": 20,
                "align": "left",
            },
            {
                "name": "Sent",
                "get": lambda data: safe_get(data, "stats", "sent"),
                "width": 8,
                "align": "right",
            },
            {
                "name": "Received",
                "get": lambda data: safe_get(data, "stats", "recv"),
                "width": 8,
                "align": "right",
            },
            {
                "name": "Latency",
                # the unicode symbol for +- is \u00B1
                "get": self.get_latency,
                "width": 20,
                "align": "right",
            },
            {
                "name": "Send To",
                "get": lambda data: ", ".join(
                    str(x) for x in safe_get(data, "send_to")
                ),
                "width": 10,
            },
        ]

        self.COMMANDS = [
            {"key": "h", "command": self.help, "help": "Show this help"},
            {"key": "up", "command": self.move_up, "help": "Previous peer in the list"},
            {"key": "down", "command": self.move_down, "help": "Next peer in the list"},
            {
                "key": "left",
                "command": self.move_left,
                "help": "Sort by previous column",
            },
            {"key": "right", "command": self.move_right, "help": "Sort by next column"},
            {"key": "q", "command": self.quit, "help": "Quit"},
            {"key": "k", "command": self.kill, "help": "Kill peer"},
            {"key": "c", "command": self.connect, "help": "Connect to peer"},
        ]

        # make Name column as wide as possible
        self.COLUMNS[2]["width"] = (
            self.width
            - sum(x["width"] for x in self.COLUMNS)
            - (1 * (len(self.COLUMNS) - 1))
        )
        self.max_cols = len(self.COLUMNS)

        # set the terminal input in one char per read
        tty.setcbreak(sys.stdin)

    def terminal_goto(self, x, y):
        print("\033[%d;%dH" % (y, x), end="")

    def get_peer_status(self, data):
        return safe_get(data, "peer", "status")

    def get_latency(self, data):
        avg = safe_get(data, "peer", "latency_ms", "average")
        if avg == "":
            return ""
        stddev = safe_get(data, "peer", "latency_ms", "stddev")
        return f"{avg}ms \u00B1 {stddev}ms"

    def print_table(self):
        rows = self.status["router"]

        def get_sort_key(row):
            value = self.COLUMNS[self.selected_col_index]["get"](row)
            if value is None or value == "":
                return "ZZZZZZZ"
            return value

        rows.sort(key=get_sort_key)
        self.selected_row = rows[self.selected_row_index]
        max_cols = self.max_cols - 1

        def print_cell(row: dict, rown: int, coln: int):
            print(self.get_color_cell(row, rown, coln), end="")
            column = self.COLUMNS[coln]
            width = column["width"]
            value = column["get"](row)
            value = str(value)[:width]
            if column.get("align") == "right":
                print("{:>{width}}".format(value, width=width), end="")
            else:
                print("{:{width}}".format(value, width=width), end="")

        def print_row(row: dict, rown: int):
            for coln, column in enumerate(self.COLUMNS):
                print_cell(row, rown, coln)
                if coln != max_cols:
                    print(" ", end="")
            print()

        for coln, column in enumerate(self.COLUMNS):
            print(self.get_color_cell({}, 0, coln), end="")
            width = column["width"]
            value = column["name"][:width]
            if column.get("align") == "right":
                print("{:>{width}}".format(value, width=width), end="")
            else:
                print("{:{width}}".format(value, width=width), end="")

            if coln != max_cols:
                print(" ", end="")
        print()

        rown = 1
        for row in self.status["router"]:
            print_row(row, rown)
            if "peers" in row:
                for peer in row["peers"]:
                    peer = {
                        "id": "",
                        "name": safe_get(peer, "remote", "name"),
                        "type": "rtppeer",
                        "peer": peer,
                    }
                    print_row(peer, rown)
            rown += 1

        self.max_rows = rown - 1
        if self.selected_row_index >= self.max_rows:
            self.selected_row_index = self.max_rows - 1
        self.selected_row = self.status["router"][self.selected_row_index]

    def get_color_cell(self, row: dict, rown: int, coln: int):
        if rown == 0:
            if coln == self.selected_col_index:
                return self.ANSI_BG_CYAN + self.ANSI_TEXT_WHITE + self.ANSI_TEXT_BOLD
            return self.ANSI_BG_PURPLE + self.ANSI_TEXT_WHITE + self.ANSI_TEXT_BOLD

        # we are in the table rows
        rown -= 1
        bg = self.ANSI_BG_BLACK
        fg = self.ANSI_TEXT_WHITE

        if rown == self.selected_row_index:
            bg = self.ANSI_BG_WHITE
            fg = self.ANSI_TEXT_BLACK

        status = self.get_peer_status(row)
        column = self.COLUMNS[coln]

        bold = ""

        if column["name"] == "Status":
            if status == "CONNECTED":
                fg = self.ANSI_TEXT_GREEN
            elif status == "CONNECTING":
                fg = self.ANSI_TEXT_YELLOW

        if row.get("id") in self.selected_row.get("send_to", []):
            bold = self.ANSI_TEXT_BOLD
            fg = self.ANSI_TEXT_YELLOW

        if self.selected_row.get("id") in row.get("send_to", []):
            fg = self.ANSI_TEXT_YELLOW
            bold = self.ANSI_TEXT_BOLD

        return self.ANSI_RESET + fg + bg + bold

    def print_header(self):
        # write a header with the rtpmidid client, version, with color until the end of line
        print(self.ANSI_BG_BLUE + self.ANSI_TEXT_BOLD + self.ANSI_TEXT_WHITE, end="")
        self.print_padding("rtpmidid-cli v23.12")
        # color until next newline
        print(self.ANSI_RESET, end="")
        print()

    def print_footer(self):
        self.terminal_goto(1, self.height)

        print(self.ANSI_BG_BLUE + self.ANSI_TEXT_BOLD + self.ANSI_TEXT_WHITE, end="")
        footer_left = f"Press Ctrl-C to exit | [q]uit | [k]ill midi peer | [c]onnect to peer | [up] [down] to navigate"
        footer_right = f"| rtpmidid-cli v23.12 | (C) Coralbits 2023"
        padding = self.width - len(footer_left) - len(footer_right)
        self.print_padding(f"{footer_left}{' ' * padding}{footer_right}")
        print(self.ANSI_RESET, end="")

    def print_padding(self, text, count=None):
        if count is None:
            count = self.width

        padchars = count - len(text)
        if padchars < 0:
            padchars = 0
        print(text[:count] + " " * padchars, end="")

    def wait_for_input(self, timeout=1):
        start = time.time()
        while True:
            if time.time() - start > timeout:
                return
            if sys.stdin in select.select([sys.stdin], [], [], 1)[0]:
                # read a key
                key = sys.stdin.read(1)
                # if key is the ansi code for arrow keys, read the next 2 bytes and return "up", "down", "left", "right"
                if key == "\033":
                    key += sys.stdin.read(2)
                    if key == "\033[A":
                        return "up"
                    elif key == "\033[B":
                        return "down"
                    elif key == "\033[C":
                        return "right"
                    elif key == "\033[D":
                        return "left"
                return key

    def parse_key(self, key):
        # up got up in the current row
        for cmd in self.COMMANDS:
            if key == cmd["key"]:
                cmd["command"]()
                return

    def move_up(self):
        self.selected_row_index -= 1
        if self.selected_row_index < 0:
            self.selected_row_index = 0

    def move_down(self):
        self.selected_row_index += 1
        if self.selected_row_index >= self.max_rows:
            self.selected_row_index = self.max_rows - 1

    def move_left(self):
        self.selected_col_index -= 1
        if self.selected_col_index < 0:
            self.selected_col_index = self.max_cols - 1

    def move_right(self):
        self.selected_col_index += 1
        if self.selected_col_index >= self.max_cols:
            self.selected_col_index = 0

    def quit(self):
        sys.exit(0)

    def kill(self):
        self.conn.command(
            {"method": "router.remove", "params": [self.selected_row["id"]]}
        )

    def connect(self):
        peer_id = self.dialog_ask("Connect to which peer id?")
        self.conn.command(
            {
                "method": "router.connect",
                "params": {"from": self.selected_row["id"], "to": int(peer_id)},
            }
        )

    def help(self):
        text = ""
        for cmd in self.COMMANDS:
            text += f"[{cmd['key']}]{' '*(8-len(cmd['key']))} {cmd['help']}\n"
        text = text.strip()
        self.dialog(text=text)

    def set_cursor(self, x, y):
        print("\033[%d;%dH" % (y, x), end="")

    def dialog(self, text):
        width = max(len(x) for x in text.split("\n")) + 2
        width_2 = width // 2
        start_x = self.width // 2 - width_2
        start_y = self.height // 3

        print(self.ANSI_BG_PURPLE + self.ANSI_TEXT_WHITE + self.ANSI_TEXT_BOLD, end="")
        self.terminal_goto(start_x, start_y)
        # self.print_padding("", width)
        # top border, width, using unicode table characters
        print(f"\u250C{chr(0x2500) * (width-1)}\u2510")
        for linen, line in enumerate(text.split("\n")):
            self.terminal_goto(start_x, start_y + linen + 1)
            self.print_padding(f"\u2502{line}", width)
            print(f"\u2502", end="")

        # bottom border, width, using unicode table characters
        self.terminal_goto(start_x, start_y + linen + 2)
        print(f"\u2514{chr(0x2500) * (width-1)}\u2518")

        # text: press any key to close dialog in the center
        self.terminal_goto(start_x + width_2 - 8, start_y + linen + 2)
        print("[ Press any key ]", end="")

        # cursor at end of screen
        self.set_cursor(self.width, self.height)

        # self.print_padding("", width)

        print(self.ANSI_RESET, flush=True, end="")
        # wait for a key
        self.wait_for_input(timeout=10000000)

    def dialog_ask(self, question, width=60):
        # print a blue box with white text
        width_2 = width // 2
        start_x = self.width // 2 - width_2
        start_y = self.height // 3

        print(self.ANSI_BG_BLUE + self.ANSI_TEXT_WHITE + self.ANSI_TEXT_BOLD, end="")
        self.terminal_goto(start_x, start_y)
        self.print_padding("", width_2)
        self.terminal_goto(start_x, start_y + 1)
        self.print_padding(" " + question, width_2)
        self.terminal_goto(start_x, start_y + 2)
        self.print_padding("", width_2)
        self.terminal_goto(start_x, start_y + 4)
        self.print_padding("", width_2)
        print(self.ANSI_RESET + self.ANSI_BG_WHITE + self.ANSI_TEXT_BLUE, end="")
        self.terminal_goto(start_x, start_y + 3)
        self.print_padding("", width_2)
        self.set_cursor(start_x + 1, start_y + 3)

        sys.stdout.flush()
        answer = ""
        while True:
            key = self.wait_for_input()
            if key == "\n":
                break
            elif key == "\x7f":
                answer = answer[:-1]
            elif key:
                answer += key
            self.terminal_goto(start_x, start_y + 3)
            self.print_padding(" " + answer, width_2)
            self.set_cursor(start_x + 1 + len(answer), start_y + 3)
            sys.stdout.flush()
        print(self.ANSI_RESET, end="")

        return answer

    def print_row(self, row):
        if not row:
            return

        text = json.dumps(row, indent=2)

        data_rows = self.max_rows + 2
        top_area = data_rows + 4
        max_col = self.height - top_area

        print(self.ANSI_RESET + self.ANSI_BG_BLUE + self.ANSI_TEXT_WHITE, end="")
        self.print_padding(f"Current Row {self.height}: ")
        print(self.ANSI_RESET, end="")
        width_2 = self.width // 2
        max_col2 = max_col * 2
        for idx, row in enumerate(text.split("\n")):
            if idx >= max_col2:
                continue  # skip too many rows
            elif idx < max_col:
                self.terminal_goto(0, top_area + idx)
                print(row)
            else:
                self.terminal_goto(width_2, top_area + idx - max_col)
                print(f"\u2502 {row}")

    def refresh_data(self):
        ret = self.conn.command({"method": "status"})
        self.status = ret["result"]

    def top_loop(self):
        print(self.ANSI_PUSH_SCREEN, end="")
        try:
            self.refresh_data()
            while True:
                print(self.ANSI_CLEAR_SCREEN, end="")
                self.print_header()
                self.print_table()
                self.print_row(self.selected_row)
                self.print_footer()
                print(flush=True, end="")
                key = self.wait_for_input()
                if key:
                    self.parse_key(key)
                else:
                    self.refresh_data()
        except KeyboardInterrupt:
            pass
        finally:
            print(self.ANSI_POP_SCREEN, end="")


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

    if not settings.command:
        return Top(conn).top_loop()

    for cmd in parse_commands(settings.command or ["help"]):
        print(">>> %s" % json.dumps(cmd), file=sys.stderr)
        ret = conn.command(cmd)
        print(json.dumps(ret, indent=2))


if __name__ == "__main__":
    main(sys.argv)
