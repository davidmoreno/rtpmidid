% RTPMIDID(1) rtpmidid 21.11
% David Moreno <dmoreno@coralbits.com>
% November 2021

# NAME

rtpmidid-cli -- Command line interface for a running rtpmidid

# SYNOPSIS

**rtpmidid-cli** \[options...\]

# DESCRIPTION

Allow to communicate witha running rtpmidid(1) instance, and inspect and change behaviour.

I always writes back JSON to allow easy reading and communicating with external programs.

Several options can be written on one invocation allowing to perform several ations in one go.

Options must be separated with **.**

# OPTIONS

**help**
: Show known options

**exit**
: Just exits

**connect** name \[host\] \[port\]
: connect with a remote server with the given. Only name is mandatory. Default for host
is the same name, and the default port is 5004. Host must be specified if need a
different port.

**status**
: show information about current server and client connections

# EXAMPLES

**rtpmidid-cli connect deepmind12 192.168.1.35 5004**

Connects with IP 192.168.1.35, port 5004, and gives local name of deepmind12.

**rtpmidid-cli connect deepmind12 192.168.1.35 5004 . connect win10 192.168.1.36 . connect mac.local**

Creates three connections: deepmind12 at 192.168.1.35:5004, win10 at 192.168.1.36:5004 and
mac.local at mac.local:5004.

# EXIT VALUES

**1**
: Error

**0**
: Success

# SEE ALSO

rtpmidid(1)

# COPYRIGHT

(C) 2019-2021 David Moreno <dmoreno@coralbits.com>.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
