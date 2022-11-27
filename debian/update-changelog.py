#!/usr/bin/python3

import os
import subprocess
import re

VERSION_RE = re.compile(r"^(.*) \((.*?)\) (.*)$")
ITEM_RE = re.compile(r"^\s*\* (.*)$")
DATETIME_RE = re.compile(r"^-- (.*? \<.*?\>)  (.*)$")


def main():
    os.chdir(os.path.dirname(__file__))

    version = subprocess.check_output(
        ["git", "describe", "--match", "v[0-9]*", "--tags", "--abbrev=5", "HEAD"]
    ).decode().strip()
    # now version is something like v21.11-32-g4debe
    version = version[1:].replace("-", "~")
    # now version is something like 21.11~32~g4debe
    datetime = subprocess.check_output(["date", "-R"]).decode().strip()

    with open("changelog", "rt") as fd:
        data = readchangelog(fd)

    # this not an development version, create it
    if '~' not in data[0]["version"]:
        data.insert(0, {**data[0]})  # prepend, reuse all possible data
        data[0]["items"] = ["Development version. Check `git log`."]

    data[0]["datetime"] = datetime
    data[0]["version"] = version

    with open("changelog", "wt") as fd:
        writechangelog(data, fd)


def readchangelog(fd):
    blocks = []
    block = {}

    for line in fd.readlines():
        line = line.strip()
        if not line:
            continue
        match = VERSION_RE.match(line)
        if match:
            block = {
                "package": match.group(1),
                "version": match.group(2),
                "suite": match.group(3),
                "author": "",
                "datetime": "",
                "items": []
            }
            blocks.append(block)
            continue
        match = ITEM_RE.match(line)
        if match:
            block["items"].append(match.group(1))
            continue
        match = DATETIME_RE.match(line)
        if match:
            block["author"] = match.group(1)
            block["datetime"] = match.group(2)
            continue
        # this must be a continuation of last item
        block["items"][len(block["items"])-1] += f" {line}"

    return blocks


def writechangelog(blocks, fd):
    for block in blocks:
        print(
            f"{block['package']} ({block['version']}) {block['suite']}", file=fd)
        print(file=fd)
        for item in block["items"]:
            print(f"  * {item}", file=fd)
        print(file=fd)
        print(f" -- {block['author']}  {block['datetime']}", file=fd)
        print(file=fd)


if __name__ == '__main__':
    main()
