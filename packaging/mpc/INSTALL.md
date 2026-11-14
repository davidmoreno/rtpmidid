# rtpmidid for Akai MPC standalone devices

RTP-MIDI (Apple "Network MIDI") for the MPC, as a background daemon:

- every RTP-MIDI endpoint found on the LAN becomes an **ALSA sequencer port**, and
  the MPC lists ALSA sequencer clients as MIDI inputs, so a peer shows up in the
  MPC's MIDI pages like any other device;
- the MPC's own hardware MIDI ports are exported to the network;
- the MPC itself is announced as an RTP-MIDI endpoint (`<hostname>.local:5004`).

It starts at boot and keeps running with the MPC application, so a network peer
is there every time a project is opened, without loading a plugin or changing a
project.

## Requirements

- An MPC running MPC OS 3.x with root SSH (Preferences → Info, developer mode;
  or the "user access" shell documented in `/usr/share/doc/inmusic`). The
  install runs as root.
- The device's internal storage (`/data`) is where the package lives: SD cards
  and USB drives are mounted `noexec`, and the daemon cannot be run from them.
- Same network segment as the RTP-MIDI peers. Discovery is mDNS
  (`_apple-midi._udp`, multicast), so it does not cross routers by default.

MPC firmware is a Yocto image with no package manager, so this is a zip with an
`install.sh`, not a `.deb`. Only the daemon is included (no `rtpmidid-cli`).

## Install

Unzip it on your computer (or on the device: it has `unzip`) and run the
installer. The zip holds one folder, the same shape the MPC plugin packages
have, including the `MODES` file that says which files have to be executable:

```sh
scp rtpmidid-<version>-mpc-armv7.zip root@<mpc>:/data/
ssh root@<mpc>
cd /data
unzip rtpmidid-<version>-mpc-armv7.zip
sh rtpmidid-<version>-mpc-armv7/install.sh
```

Unpacking it onto a USB stick or SD card and running `install.sh` from there
works too, but the payload always ends up on `/data`, because the drives are
mounted `noexec`.

`armv7` is Gen1 hardware (MPC One, Live, X, Force…), `aarch64` is Gen2 (MPC Live
III, MPC XL, 2023+ units). The installer refuses to install the wrong one.

Options:

```sh
sh install.sh -t /data/rtpmidid      # install somewhere else
sh install.sh -y                     # answer yes to every question
sh install.sh -n                     # do not restart the MPC application
sh install.sh --force-avahi          # take over an existing avahi-daemon.service
                                     # instead of adding a drop-in next to it
sh install.sh --reinstall            # overwrite everything, ini included
sh install.sh --etc-units            # units in /etc instead of the rootfs (no autostart)
sh install.sh --uninstall            # remove everything again
```

`--reinstall` is the "give me the packaged files, I do not care what I changed"
switch: it does not ask before replacing the existing installation and it
overwrites `rtpmidid.ini` with the packaged one (a normal install keeps a
hand-edited ini and leaves the new defaults in `rtpmidid.ini.dist`).

It asks before it replaces an existing installation, before it restarts the MPC
and before it touches anything of the firmware's; `-y` skips the questions.

The installer:

1. copies the payload to `/data/rtpmidid` (`bin`, `lib`, `etc`, `services`);
2. writes `rtpmidid.service`, `avahi-mpc.service` and their
   `multi-user.target.wants` entries into `/usr/lib/systemd/system`, the firmware
   rootfs (`/` is remounted read-write for the second it takes, then read-only
   again). This is the only place systemd looks at when it boots: units that only
   exist in the `/etc` overlay are not seen then, which is why every MPC-specific
   unit has to live here. `--etc-units` keeps them in `/etc` instead, knowing
   they will not start by themselves;
3. runs `rtpmidid --version` to prove the binary loads on the device;
4. sorts out mDNS: the MPC has no avahi at all, so unless one is already
   installed it starts the bundled `avahi-daemon` (as `avahi-mpc.service`, or
   through a leftover `avahi-daemon.service` from an earlier manual install).
   `avahi-daemon` insists on a root process with a resolvable `avahi` account,
   so the installer creates one with `adduser` when `/etc` is writable, and
   leaves it in place when you uninstall. With `/etc` read-only (the usual case
   once user access is enabled) it cannot, so the unit gets a private copy of
   `passwd`/`group` under `/data/rtpmidid/etc/avahi-user` bind-mounted
   read-only into avahi-daemon only — nothing under `/etc` is written, so
   `getent passwd avahi` still finds nothing outside that unit. It also adds the
   avahi D-Bus policy if the device lacks it;
5. starts the services and offers to restart the MPC application so it picks up
   the new MIDI ports.

Re-running the installer upgrades in place: the payload is replaced, your
`rtpmidid.ini` is kept (the shipped defaults land next to it as
`rtpmidid.ini.dist`), and anything the previous install changed outside the
target is remembered so `--uninstall` can undo it.

The units go into the firmware rootfs, so a firmware update removes them: after
updating, run the installer again (it says so at the end).

## Check that it works

```sh
systemctl status rtpmidid.service avahi-daemon
journalctl -u rtpmidid.service -f

cat /proc/asound/seq/clients                 # kernel view, no tools needed
/data/rtpmidid/bin/aconnect -l               # same thing, more readable
/data/rtpmidid/bin/aconnect 20:0 128:2       # wire a port by hand (from → to)
```

On the MPC: Preferences → MIDI (or the track's MIDI input page). The daemon's
ports are named after the peers found, and `rtpmidid`/`Network Export` for local
use. The list is read when the MPC application starts, so restart it
(`systemctl restart acvs`) if a peer appeared later.

From another machine, `avahi-browse -rt _apple-midi._udp` should list the MPC.

## Configuration

`/data/rtpmidid/etc/rtpmidid.ini` (see the comments in it). After editing:

```sh
systemctl restart rtpmidid.service
```

The sections that matter on an MPC:

- `[rtpmidi_announce]` — this device's announced name and port (5004).
- `[rtpmidi_discover]` — which discovered peers to expose; restrict with the
  `name_positive_regex` / `name_negative_regex` if the LAN has endpoints you do
  not want on the device. The default drops the peers' own ALSA system clients
  (`System-Announce`, `System-Timer`, `Midi Through`) because they never accept
  a session and only add entries to the MIDI input list.
- `[alsa_announce]` — the local `Network Export` port.
- `[alsa_hw_auto_export]` — export local hardware MIDI ports to the network.
  The MPC application wants exclusive use of its own ports (kernel client `MPC
  One MIDI`, ports `MPC Studio Live …`) and of the ALSA system clients, so they
  are filtered out by `name_negative_regex`. That key uses `regex_match`: the
  **whole** name must match, hence the `.*` on both sides. A USB keyboard plugged
  into the MPC is still exported; add its name to the filter to keep it local.
  Comment out the section to keep the device receive-only.
- `[connect_to]` — fixed `hostname`/`port` peers, for networks where mDNS is
  filtered. Can be repeated.

Upgrades keep your file and drop the shipped defaults next to it as
`rtpmidid.ini.dist`.

## Uninstall

```sh
sh /data/rtpmidid-<version>-mpc-<arch>/install.sh --uninstall
# or, from the package directory:  sh uninstall.sh
```

(Keep the unpacked package around, or fetch it again; the script needs nothing
but itself.) It asks once (`-y` to skip), then stops the daemon, restores the
original avahi unit if `--force-avahi` had replaced it, and removes the units,
the payload and the D-Bus policy it added. With `/etc` read-only, the unit files
leave `/etc` at the next reboot.

## Troubleshooting

| Symptom | What to look at |
|---|---|
| `systemctl status rtpmidid.service` fails with a library error | the payload is for the wrong architecture, or the device is not the model you think: re-run the installer and read its `--version` smoke test |
| `avahi-daemon: Failed to find user 'avahi'` | the bundled daemon needs that account; reinstall with a package that has the fix, or `adduser -S -D -H -s /bin/false avahi` yourself if `/etc` is writable |
| daemon runs, no peers listed | `journalctl -u avahi-daemon`: mDNS has to see the interface (`ip addr`); check the peer really announces `_apple-midi._udp`; the LAN may filter multicast |
| peers listed, nothing arrives | RTP-MIDI uses UDP 5004 (control) and 5005 (data) to this device. Test the same peer with a computer first |
| MPC does not show the ports | the MPC application enumerates at start: `systemctl restart acvs` |
| the daemon is not running after a reboot | the units live in the firmware rootfs: check `systemctl is-enabled rtpmidid.service`; if a firmware update removed them, re-run the installer |
| units were installed with `--etc-units` | they are in `/etc` and will not start by themselves: `systemctl start rtpmidid.service`, or reinstall without `--etc-units` |
| ports doubled / two mDNS responders | the firmware's own `DeviceControlServer` also binds 5353 for `_inmusic-device._tcp`; it is a different service and shares the socket, but do not install a third avahi |
| wrong name announced | `[rtpmidi_announce] name={{hostname}}` follows the device hostname; set it explicitly to override |
