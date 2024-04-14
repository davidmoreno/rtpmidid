#!/usr/bin/python3

from dataclasses import dataclass
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
        self.socket.connect(filename)

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

Each peer may have its own commands. Check with `[peer_id].help`.

Arguments will be passed as a list or key=value into a dict. For example:

1.remove_endpoint hostname=192.168.122.1 port=5004

"""
    )
    parser.add_argument("--control")
    parser.add_argument("command", nargs="*")

    return parser.parse_args(argv[1:])


def parse_commands(argv):
    def guess_type(value: str):
        # returns a str, or int
        if value == "true":
            return True
        elif value == "false":
            return False
        elif value == "null":
            return None
        try:
            return int(value)
        except:
            pass
        return value

    def prepare_params(cmd: list):
        # if  = in the list values, then convert to dict
        if not cmd:
            return cmd
        if "=" in cmd[0]:
            d = {}
            for x in cmd:
                k, v = x.split("=")
                d[k] = guess_type(v)
            return d
        return [guess_type(x) for x in cmd]

    cmd = []
    for x in argv:
        if x == ".":
            yield {"method": cmd[0], "params": prepare_params(cmd[1:])}
            cmd = []
        else:
            cmd.append(x)
    if cmd:
        yield {"method": cmd[0], "params": prepare_params(cmd[1:])}


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
    ANSI_BG_DARK_BLUE = "\033[48;5;18m"

    ANSI_RESET = "\033[0m"

    @dataclass
    class Tabs:
        ROUTES = 1
        MDNS = 2

    tab: Tabs = Tabs.ROUTES

    def __init__(self, conn: Connection):
        # terminal width and height from stty
        self.conn = conn
        width, height = shutil.get_terminal_size()
        self.selected_row_index = 0
        self.selected_col_index = 0
        self.width = width
        self.height = height
        self.status = {}
        self.expand_peers = False

        self.STATUS_STYLE = {
            "WAITING": self.ANSI_TEXT_YELLOW,
            "CONNECTING": self.ANSI_BG_PURPLE,
            "CONNECTED": self.ANSI_TEXT_GREEN,
            "DISCONNECTED": self.ANSI_TEXT_RED,
        }

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
                "name": "Local Name",
                "get": self.get_local_name,
                "width": 0,
                "align": "left",
            },
            {
                "name": "Remote Name",
                "get": self.get_remote_name,
                "width": 0,
                "align": "left",
            },
            {
                "name": "Status",
                "get": self.get_peer_status,
                "width": 20,
                "align": "left",
                "style": lambda x: self.STATUS_STYLE.get(self.get_peer_status(x)),
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
            {"key": "h", "command": self.command_help, "help": "Show this help"},
            {
                "key": "up",
                "command": self.command_move_up,
                "help": "Previous peer in the list",
            },
            {
                "key": "down",
                "command": self.command_move_down,
                "help": "Next peer in the list",
            },
            {
                "key": "left",
                "command": self.command_move_left,
                "help": "Sort by previous column",
            },
            {
                "key": "right",
                "command": self.command_move_right,
                "help": "Sort by next column",
            },
            {"key": "q", "command": self.command_quit, "help": "Quit"},
            {
                "key": "k",
                "command": self.command_kill,
                "help": "Kill peer",
                "ontab": self.Tabs.ROUTES,
            },
            {
                "key": "c",
                "command": self.command_connect,
                "help": "Connect to peer",
                "ontab": self.Tabs.ROUTES,
            },
            {
                "key": "supr",
                "command": self.command_delete_mdns_entry,
                "help": "Delete mdns entry",
                "ontab": self.Tabs.MDNS,
            },
            {
                "key": "p",
                "command": self.command_expand_peers,
                "help": "Toggle expand peers",
            },
            {"key": "tab", "command": self.command_switch_tab, "help": "Switch tabs"},
        ]

        # make Name column as wide as possible
        auto_width_count = len([x for x in self.COLUMNS if x["width"] == 0])
        extra_width = self.width - sum((x["width"] + 1) for x in self.COLUMNS) + 1
        for col in self.COLUMNS:
            if col["width"] == 0:
                col["width"] = extra_width // auto_width_count
        self.max_cols = len(self.COLUMNS)
        self.print_data = []

        # set the terminal input in one char per read
        tty.setcbreak(sys.stdin)

    debug_lines = []

    def debug(self, txt):
        self.debug_lines.append(txt)
        return txt

    ##
    ## INPUT
    ##

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
                    elif key == "\033\x1b":
                        return "escape"
                    return None
                if key == "\t":
                    return "tab"

                return key

    def parse_key(self, key):
        # up got up in the current row
        for cmd in self.COMMANDS:
            if key == cmd["key"]:
                cmd["command"]()
                return

    def command_move_up(self):
        self.selected_row_index -= 1
        if self.selected_row_index < 0:
            self.selected_row_index = 0

    def command_move_down(self):
        self.selected_row_index += 1

    def command_move_left(self):
        self.selected_col_index -= 1
        if self.selected_col_index < 0:
            self.selected_col_index = self.max_cols - 1

    def command_move_right(self):
        self.selected_col_index += 1
        if self.selected_col_index >= self.max_cols:
            self.selected_col_index = 0

    def command_quit(self):
        sys.exit(0)

    def command_kill(self):
        data = self.status["router"]
        current_id = data[self.selected_row_index]["id"]
        self.conn.command({"method": "router.remove", "params": [current_id]})

    def command_connect(self):
        data = self.status["router"]
        current_id = data[self.selected_row_index]["id"]
        peer_id = self.dialog_ask("Connect to which peer id?")
        if not peer_id:
            return

        self.conn.command(
            {
                "method": "router.connect",
                "params": {"from": current_id, "to": int(peer_id)},
            }
        )

    def command_expand_peers(self):
        self.expand_peers = not self.expand_peers

    def command_switch_tab(self):
        if self.tab == self.Tabs.ROUTES:
            self.tab = self.Tabs.MDNS
        else:
            self.tab = self.Tabs.ROUTES

    def command_delete_mdns_entry(self):
        pass

    def command_help(self):
        text = ""
        for cmd in self.COMMANDS:
            ontab = cmd.get("ontab")
            if ontab is not None and ontab != self.tab:
                continue
            text += f"[{cmd['key']}]{' '*(8-len(cmd['key']))} {cmd['help']}\n"
        text = text.strip()
        self.dialog(text=text)

    ##
    ## BASIC OUTPUT
    ##

    def print(self, text):
        self.print_data.append(text)

    def flush(self):
        print("".join(self.print_data), end="", flush=True)
        self.print_data = []

    def terminal_goto(self, x, y):
        self.print("\033[%d;%dH" % (y, x))

    def set_cursor(self, x, y):
        self.print("\033[%d;%dH" % (y, x))

    def print_square(self, x, y, width, height, color):
        self.print(color)
        for i in range(height):
            self.terminal_goto(x, y + i)
            self.print(" " * width)
        self.print(self.ANSI_RESET)

    def dialog(self, text, bottom="Press any key", wait_for_key=True):
        width = max(len(x) for x in text.split("\n")) + 2
        width_2 = width // 2
        start_x = self.width // 2 - width_2
        start_y = self.height // 3

        self.print(self.ANSI_BG_PURPLE + self.ANSI_TEXT_WHITE + self.ANSI_TEXT_BOLD)
        self.terminal_goto(start_x, start_y)
        # self.print_padding("", width)
        # top border, width, using unicode table characters
        self.print(f"\u250C{chr(0x2500) * (width-1)}\u2510")
        for linen, line in enumerate(text.split("\n")):
            self.terminal_goto(start_x, start_y + linen + 1)
            self.print_padding(f"\u2502{line}", width)
            self.print(f"\u2502")

        # bottom border, width, using unicode table characters
        self.terminal_goto(start_x, start_y + linen + 2)
        self.print(f"\u2514{chr(0x2500) * (width-1)}\u2518")

        # text: press any key to close dialog in the center
        self.terminal_goto(
            start_x + width_2 - 2 - len(bottom) // 2, start_y + linen + 2
        )
        self.print(f"[ {bottom} ]")

        # cursor at end of screen
        self.set_cursor(self.width, self.height)

        # self.print_padding("", width)

        self.print(self.ANSI_RESET)
        self.flush()
        # wait for a key
        if wait_for_key:
            self.wait_for_input(timeout=10000000)

    def dialog_ask(self, question, width=60):
        # print a blue box with white text
        width_2 = width // 2
        start_x = self.width // 2 - width_2
        start_y = self.height // 3

        self.print(self.ANSI_BG_BLUE + self.ANSI_TEXT_WHITE + self.ANSI_TEXT_BOLD)
        self.terminal_goto(start_x, start_y)
        self.print_padding("", width_2)
        self.terminal_goto(start_x, start_y + 1)
        self.print_padding(" " + question, width_2)
        self.terminal_goto(start_x, start_y + 2)
        self.print_padding("", width_2)
        self.terminal_goto(start_x, start_y + 4)
        self.print_padding("", width_2)
        self.print(self.ANSI_RESET + self.ANSI_BG_WHITE + self.ANSI_TEXT_BLUE)
        self.terminal_goto(start_x, start_y + 3)
        self.print_padding("", width_2)
        self.set_cursor(start_x + 1, start_y + 3)

        self.flush()
        answer = ""
        while True:
            key = self.wait_for_input(timeout=10000000)
            if key is None:
                return None
            elif key == "escape":
                return None
            elif key == "\n":
                break
            elif key == "\x7f":
                answer = answer[:-1]
            elif key:
                answer += key
            self.terminal_goto(start_x, start_y + 3)
            self.print_padding(" " + answer, width_2)
            self.set_cursor(start_x + 1 + len(answer), start_y + 3)
            self.flush()
        self.print(self.ANSI_RESET)
        self.print_all()

        return answer

    def print_padding(self, text, count=None):
        if count is None:
            count = self.width

        padchars = count - len(text)
        if padchars < 0:
            padchars = 0
        self.print(text[:count] + " " * padchars)

    ##
    ## Data gathering
    ##

    def get_peer_status(self, data):
        return safe_get(data, "peer", "status") or safe_get(data, "status")

    def get_latency(self, data):
        avg = safe_get(data, "peer", "latency_ms", "average")
        if avg == "":
            return ""
        stddev = safe_get(data, "peer", "latency_ms", "stddev")
        return f"{avg}ms \u00B1 {stddev}ms"

    def get_local_name(self, data):
        type_ = safe_get(data, "type")
        if type_.startswith("local:"):
            return safe_get(data, "name")
        return ""

    def get_remote_name(self, data):
        type_ = safe_get(data, "type")
        if type_.startswith("network:"):
            return safe_get(data, "name")
        return ""

    ##
    ## Top level components
    ##

    def print_header(self):
        # write a header with the rtpmidid client, version, with color until the end of line
        self.print(self.ANSI_BG_BLUE + self.ANSI_TEXT_BOLD + self.ANSI_TEXT_WHITE)
        self.print_padding("rtpmidid-cli v23.12")
        # color until next newline
        self.print(self.ANSI_RESET)
        self.print("\n")

    def print_footer(self):
        self.terminal_goto(1, self.height)

        self.print(self.ANSI_BG_BLUE + self.ANSI_TEXT_BOLD + self.ANSI_TEXT_WHITE)
        footer_left = f"Press Ctrl-C to exit | [q]uit | [k]ill midi peer | [c]onnect to peer | [up] [down] to navigate"
        footer_right = f"| rtpmidid-cli v23.12 | (C) Coralbits 2023"
        padding = self.width - len(footer_left) - len(footer_right)
        self.print_padding(f"{footer_left}{' ' * padding}{footer_right}")
        self.print(self.ANSI_RESET)

    def print_row(self, x, y, width, height, data):
        if not data:
            return

        text = json.dumps(data, indent=2)

        top_area = y
        max_col = y + height - top_area

        self.print(self.ANSI_RESET)
        width_2 = width // 2
        max_col2 = max_col * 2
        for idx, row in enumerate(text.split("\n")):
            if idx >= max_col2:
                continue  # skip too many rows
            elif idx < max_col:
                self.terminal_goto(x, top_area + idx)
                self.print(row[:width_2])
            else:
                self.terminal_goto(x + width_2, top_area + idx - max_col)
                self.print(f"\u2502 {row[:width_2-2]}")

    def print_all(self):
        self.print(self.ANSI_CLEAR_SCREEN)
        self.print_header()
        self.print_tabs()
        if self.tab == self.Tabs.ROUTES:
            self.print_routes_tab()
        elif self.tab == self.Tabs.MDNS:
            self.print_mdns_tab()
        else:
            self.print_clean_tab()
        self.print_footer()
        self.flush()

    def print_tabs(self):
        selected = self.ANSI_BG_BLUE + self.ANSI_TEXT_WHITE + self.ANSI_TEXT_BOLD
        not_selected = self.ANSI_BG_BLACK + self.ANSI_TEXT_WHITE

        self.terminal_goto(0, 3)
        self.print(self.ANSI_BG_BLACK + "  ")
        if self.tab == self.Tabs.ROUTES:
            self.print(selected + " Routes ")
        else:
            self.print(not_selected + " Routes ")
        if self.tab == self.Tabs.MDNS:
            self.print(selected + " mDNS ")
        else:
            self.print(self.ANSI_BG_BLACK + self.ANSI_TEXT_WHITE + " mDNS ")
        self.print(not_selected)
        self.print_padding("", self.width - 20)

    def print_routes_tab(self):
        data = self.status["router"]
        self.print_data_table(
            0,
            4,
            self.width,
            len(data) + 4,
            self.COLUMNS,
            data,
        )
        self.terminal_goto(0, len(data) + 5)
        self.print(self.ANSI_RESET + self.ANSI_BG_BLUE + self.ANSI_TEXT_WHITE)
        self.print_padding(f"Current Row {self.height}: ")
        self.print_row(
            0,
            6 + len(data),
            self.width,
            self.height - len(data) - 5,
            data[self.selected_row_index],
        )

    def print_mdns_tab(self):
        self.print_data_table(
            0,
            4,
            self.width,
            self.height - 2,
            [
                {
                    "name": "Name",
                    "width": 40,
                    "get": lambda data: safe_get(data, "name"),
                },
                {
                    "name": "Hostname",
                    "width": 40,
                    "get": lambda data: safe_get(data, "hostname"),
                },
                {
                    "name": "Port",
                    "width": 8,
                    "get": lambda data: safe_get(data, "port"),
                },
            ],
            self.status["mdns"]["announcements"]
            + self.status["mdns"]["remote_announcements"],
        )
        # self.print_row(0, 4, self.width, self.height - 2, self.status["mdns"])

    def print_clean_tab(self):
        self.print_square(0, 3, self.width, self.height - 2, self.ANSI_BG_DARK_BLUE)

    def print_data_table(self, x, y, width, height, columns, data, style={}):
        if self.selected_col_index >= len(columns):
            self.selected_col_index = 0
        if self.selected_row_index >= len(data):
            self.selected_row_index = len(data) - 1

        self.terminal_goto(x, y)
        colwidths = []
        weight_width = 0
        for column in columns:
            weight_width += column["width"]
        for column in columns:
            colwidths.append(int(column["width"] / weight_width * width) - 1)

        self.print(self.ANSI_TEXT_BOLD)
        for idx, column in enumerate(columns):
            colwidth = colwidths[idx]
            if idx == self.selected_col_index:
                self.print(style.get("header_bg_color:selected", self.ANSI_BG_CYAN))
                self.print(
                    style.get("header_text_color:selected", self.ANSI_TEXT_WHITE)
                )
            else:
                self.print(style.get("header_bg_color", self.ANSI_BG_PURPLE))
                self.print(style.get("header_text_color", self.ANSI_TEXT_WHITE))

            self.print_padding(column["name"], colwidth)
            self.print(" ")
        self.terminal_goto(x, y + 1)

        sortf = columns[self.selected_col_index].get("get_sort_key")
        if not sortf:
            sortf = columns[self.selected_col_index]["get"]

        sorted_data = sorted(data, key=lambda x: sortf(x))

        self.print(self.ANSI_RESET)
        for idx, row in enumerate(sorted_data[: height - 1]):
            self.terminal_goto(x, y + 1 + idx)
            for column, colwidth in zip(columns, colwidths):
                if idx == self.selected_row_index:
                    self.print(style.get("row_bg_color:selected", self.ANSI_BG_WHITE))
                    self.print(
                        style.get("row_text_color:selected", self.ANSI_TEXT_BLACK)
                    )
                else:
                    self.print(style.get("row_bg_color", self.ANSI_BG_BLACK))
                    self.print(style.get("row_text_color", self.ANSI_TEXT_WHITE))

                cstyle = column.get("style")
                if cstyle:
                    cstyle = cstyle(row)
                    if cstyle:
                        self.print(cstyle)

                value = column["get"](row)
                if column.get("align") == "right":
                    self.print("{:>{width}}".format(value, width=colwidth))
                else:
                    self.print("{:{width}}".format(value, width=colwidth))
                # self.print_padding(str(value or ""), colwidth)
                self.print(" ")
            self.terminal_goto(x, y + 1)

    def refresh_data(self):
        try:
            ret = self.conn.command({"method": "status"})
        except BrokenPipeError:
            self.dialog(
                "Connection to rtpmidid lost. Reconnecting...",
                bottom="Press Control-C to exit",
                wait_for_key=False,
            )
            while True:
                time.sleep(1)
                try:
                    self.conn = Connection(self.conn.filename)
                    ret = self.conn.command({"method": "status"})
                    break
                except:
                    pass

        self.status = ret["result"]

    def top_loop(self):
        self.print(self.ANSI_PUSH_SCREEN)
        try:
            self.refresh_data()
            while True:
                self.print_all()
                key = self.wait_for_input()
                if key:
                    self.parse_key(key)
                else:
                    self.refresh_data()
        except KeyboardInterrupt:
            pass
        finally:
            self.print(self.ANSI_POP_SCREEN)
            tty.setcbreak(sys.stdin)
            print("\033[?1049l", end="")
            print("\n".join(str(x) for x in self.debug_lines if x))


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
