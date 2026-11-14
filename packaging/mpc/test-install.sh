#!/bin/bash
# Dry-run install.sh / uninstall.sh of an MPC package inside a container.
#
#   packaging/mpc/test-install.sh [armv7|aarch64]
#
# The container is a plain Debian bookworm with only the libraries an MPC
# provides (libasound2, libdbus-1-3, libexpat1, libcap2, libstdc++6), so this
# also proves the payload does not rely on anything else. systemctl is stubbed;
# /dev/snd/seq is faked with mknod because install.sh requires a character
# device there.
#
# The default unit location is the firmware rootfs (/usr/lib/systemd/system):
# units that only exist in the /etc overlay do not start at boot on these
# devices. --etc-units keeps the old overlay behaviour and is exercised here with
# a fake mountinfo and a failing write probe, so both paths are covered.
set -euo pipefail

ARCH=${1:-armv7}
case "$ARCH" in
  armv7) PLATFORM=linux/arm/v7 ;;
  aarch64) PLATFORM=linux/arm64 ;;
  *)
    echo "usage: $0 [armv7|aarch64]" >&2
    exit 1
    ;;
esac

HERE=$(cd "$(dirname "$0")" && pwd)
DIST=$(cd "$HERE/../dist/mpc-$ARCH" && pwd) ||
  {
    echo "error: no packages in $HERE/../dist/mpc-$ARCH (run: make -C packaging mpc ARCH=$ARCH)" >&2
    exit 1
  }

echo "==> testing $ARCH package in a $PLATFORM container"
# Pin the local debian:bookworm tag to this platform: the image store keeps one
# manifest per tag, and the other architecture's copy would be used otherwise.
docker pull --quiet --platform "$PLATFORM" debian:bookworm >/dev/null
docker run --rm -i --platform "$PLATFORM" -v "$DIST:/pkg:ro" \
  -e MPC_ARCH="$ARCH" debian:bookworm sh -s <<'INNER'
set -eu
export DEBIAN_FRONTEND=noninteractive
fail() { echo "FAIL: $*" >&2; exit 1; }
ok() { echo "ok: $*"; }

# Exactly what an MPC One has for these binaries.
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq --no-install-recommends \
  libasound2 libdbus-1-3 libexpat1 libcap2 libstdc++6 binutils unzip file >/dev/null 2>&1
mkdir -p /dev/snd
mknod /dev/snd/seq c 116 1

# systemctl stub. FAKE_AVAHI_UNIT=1 pretends the device has an avahi-daemon unit
# (like the leftover on a real MPC); FAKE_UNIT_INVISIBLE=1 pretends systemd never
# saw the unit it was asked about.
cat >/usr/local/bin/systemctl <<'EOF'
#!/bin/sh
echo "systemctl $*" >>/tmp/systemctl.log
if [ "$1" = cat ] && [ "$2" = avahi-daemon.service ] &&
  [ "${FAKE_AVAHI_UNIT:-1}" != 1 ]; then
  exit 1
fi
if [ "$1" = cat ] && [ "$2" = rtpmidid.service ] &&
  [ "${FAKE_UNIT_INVISIBLE:-0}" = 1 ]; then
  exit 1
fi
if [ "$1" = is-enabled ]; then exit 1; fi
exit 0
EOF
chmod +x /usr/local/bin/systemctl

# qemu-user reports the host kernel's machine name (armv7l) even inside an arm64
# container, so uname is stubbed to make install.sh's architecture guard testable.
mkfake_uname() {
  cat >/usr/local/bin/uname <<EOF
#!/bin/sh
if [ "\$1" = "-m" ]; then
  echo "$1"
else
  /bin/uname "\$@"
fi
EOF
  chmod +x /usr/local/bin/uname
}
mkfake_uname "$MPC_ARCH"

cd /tmp
ZIP=$(ls /pkg/rtpmidid-*-mpc-"$MPC_ARCH".zip)
unzip -q "$ZIP"
PKG=$(cd "$(basename "$ZIP" .zip)" && pwd)
[ -f "$PKG/MODES" ] || fail "the zip has no MODES file"
grep -q "payload/bin/rtpmidid" "$PKG/MODES" || fail "MODES does not list the daemon"
[ -f "$PKG/SHA256SUMS" ] || fail "the zip has no SHA256SUMS"
echo "ok: zip contains MODES and SHA256SUMS"

# Simulate a copy through a zip or a vfat stick, which loses the exec bits.
chmod 644 "$PKG"/payload/bin/* "$PKG"/payload/lib/*
chmod 644 "$PKG"/install.sh "$PKG"/uninstall.sh
echo "ok: exec bits stripped from the unpacked package (as a USB stick would)"

DOMAIN=/data/rtpmidid
ROOTFS=/usr/lib/systemd/system
ETCUNIT=/etc/systemd/system

# --- 1. install: units in the firmware rootfs by default ------------------
sh "$PKG/install.sh" -n >/tmp/install.log 2>&1 ||
  {
    cat /tmp/install.log
    fail "install.sh failed"
  }
[ -x "$DOMAIN/bin/rtpmidid" ] || fail "daemon not installed"
[ -x "$DOMAIN/bin/avahi-daemon" ] || fail "avahi-daemon not installed"
[ -f "$DOMAIN/lib/libavahi-core.so.7" ] || fail "avahi libraries not installed"
[ -x "$DOMAIN/bin/aconnect" ] || fail "aconnect was not installed"
file "$DOMAIN/bin/aconnect" | grep -qE "ELF.*(executable|shared object)" ||
  fail "aconnect is not an ELF binary"
grep -q "payload/bin/aconnect" "$PKG/MODES" || fail "MODES does not list aconnect"
ok "aconnect ships with the payload (the MPC has no aconnect of its own)"
grep -q "rtpmidid version [0-9]" /tmp/install.log || fail "version smoke test did not run"
env -u LD_LIBRARY_PATH "$DOMAIN/bin/rtpmidid" --version >/dev/null ||
  fail "rtpmidid needs LD_LIBRARY_PATH"
env -u LD_LIBRARY_PATH "$DOMAIN/bin/avahi-daemon" --version >/dev/null ||
  fail "avahi-daemon needs LD_LIBRARY_PATH"
ok "payload runs with only device libraries, exec bits restored"

UNIT=$ROOTFS/rtpmidid.service
[ -f "$UNIT" ] || fail "the unit was not installed into the rootfs"
if grep -q "@PREFIX@" "$UNIT"; then fail "@PREFIX@ not substituted in the unit"; fi
grep -q "ExecStart=/data/rtpmidid/bin/rtpmidid --ini=/data/rtpmidid/etc/rtpmidid.ini" "$UNIT" ||
  fail "unexpected ExecStart"
grep -q "Before=acvs.service" "$UNIT" || fail "unit does not start before the MPC"
grep -q "Wants=.*avahi-daemon.service" "$UNIT" || fail "unit does not pull avahi in"
[ -L "$ROOTFS/multi-user.target.wants/rtpmidid.service" ] || fail "unit not enabled"
[ -f "$ROOTFS/avahi-mpc.service" ] || fail "no bundled avahi unit in the rootfs"
[ -f "$DOMAIN/.rootfs-units" ] || fail "no marker for the rootfs units"
if [ -e "$ETCUNIT/rtpmidid.service" ]; then
  fail "an /etc copy was written although the units go to the rootfs"
fi
if [ -e "$ETCUNIT/avahi-daemon.service.d/50-rtpmidid-mpc.conf" ]; then
  fail "an /etc drop-in was written although the units go to the rootfs"
fi
ok "install: units in the firmware rootfs, enabled, avahi bundled"

grep -q '^avahi:' /etc/passwd || fail "/etc is writable but no avahi account was created"
grep -q 'name_negative_regex=(System|Midi Through)' "$DOMAIN/etc/rtpmidid.ini" ||
  fail "MPC ini lost its name_negative_regex default"
grep -q '^address_from_mdns=true' "$DOMAIN/etc/rtpmidid.ini" ||
  fail "MPC ini lost address_from_mdns"
# the MPC application needs its own MIDI ports (and the ALSA system clients) to
# itself: they must be excluded from hw_auto_export, with a full-match regex
grep -q '^name_negative_regex=.*(System|Timer|Announce|Midi Through|MPC|Studio Live|JUCE).*' \
  "$DOMAIN/etc/rtpmidid.ini" || fail "MPC ini does not exclude the MPC's own MIDI ports"
ok "ini has the MPC defaults (mdns addresses, system-client filter)"

# --- 2. the architecture guard rejects the other generation ----------------
OTHER_ARCH=armv7
if [ "$MPC_ARCH" = armv7 ]; then OTHER_ARCH=aarch64; fi
mkfake_uname "$OTHER_ARCH"
if sh "$PKG/install.sh" -n >/tmp/wrongarch.log 2>&1; then
  mkfake_uname "$MPC_ARCH"
  fail "accepted a package for $MPC_ARCH on a $OTHER_ARCH device"
fi
mkfake_uname "$MPC_ARCH"
grep -q "this package is for" /tmp/wrongarch.log || fail "wrong-arch message missing"
ok "architecture guard"

# --- 3. ini: unedited follows new defaults, edited is kept ----------------
echo "# stale default from a previous package" >>"$DOMAIN/etc/rtpmidid.ini"
cp -a "$DOMAIN/etc/rtpmidid.ini" "$DOMAIN/etc/rtpmidid.ini.dist"
sh "$PKG/install.sh" -n -y >/tmp/iniupd.log 2>&1 || fail "reinstall failed"
if grep -q "stale default from a previous package" "$DOMAIN/etc/rtpmidid.ini"; then
  fail "an unedited ini was not updated to the package defaults"
fi
ok "an unedited ini picks up changed package defaults"

echo "# mine" >>"$DOMAIN/etc/rtpmidid.ini"
sh "$PKG/install.sh" -n -y >/tmp/iniupd2.log 2>&1 || fail "reinstall failed"
grep -q "# mine" "$DOMAIN/etc/rtpmidid.ini" || fail "a hand-edited ini was overwritten"
ok "a hand-edited ini survives an upgrade"

# --- 4. --reinstall overwrites an edited ini, without asking --------------
sh "$PKG/install.sh" -n --reinstall </dev/null >/tmp/reinstall.log 2>&1 ||
  {
    cat /tmp/reinstall.log
    fail "--reinstall failed (it must not ask anything)"
  }
grep -q '# mine' "$DOMAIN/etc/rtpmidid.ini" && fail "--reinstall kept the edited ini"
grep -q '^address_from_mdns=true' "$DOMAIN/etc/rtpmidid.ini" ||
  fail "--reinstall did not restore the packaged ini"
grep -q "overwriting $DOMAIN" /tmp/reinstall.log || fail "no --reinstall message"
ok "--reinstall overwrites the installed ini without asking"

# --- 5. no overwrite without a yes ----------------------------------------
if printf 'n\n' | sh "$PKG/install.sh" -n >/tmp/refused.log 2>&1; then
  fail "overwrote an existing installation after refusing"
fi
[ -x "$DOMAIN/bin/rtpmidid" ] || fail "refusing the overwrite removed the payload"
grep -q "cancelled" /tmp/refused.log || fail "no cancellation message"
ok "asks before overwriting an existing installation"

# --- 6. --etc-units: the old /etc-overlay behaviour -----------------------
sh "$PKG/uninstall.sh" -y >/dev/null 2>&1 || fail "uninstall failed"
sh "$PKG/install.sh" -n -y --etc-units >/tmp/etcunits.log 2>&1 ||
  {
    cat /tmp/etcunits.log
    fail "install --etc-units failed"
  }
[ -f "$ETCUNIT/rtpmidid.service" ] || fail "--etc-units did not use /etc/systemd/system"
[ -L "$ETCUNIT/multi-user.target.wants/rtpmidid.service" ] || fail "unit not enabled in /etc"
[ -f "$ETCUNIT/avahi-daemon.service.d/50-rtpmidid-mpc.conf" ] ||
  fail "no avahi drop-in for the leftover avahi unit"
if [ -e "$ROOTFS/rtpmidid.service" ]; then fail "--etc-units left a rootfs copy"; fi
ok "--etc-units keeps the units (and the drop-in) in /etc"

# --- 7. --force-avahi replaces the unit, uninstall restores it ------------
printf '[Unit]\nDescription=stub pre-existing avahi\n[Service]\nExecStart=/usr/sbin/avahi-daemon -s\n' \
  >/etc/systemd/system/avahi-daemon.service
sh "$PKG/install.sh" -n -y --etc-units --force-avahi >/tmp/force.log 2>&1 ||
  {
    cat /tmp/force.log
    fail "install --force-avahi failed"
  }
grep -q "ExecStart=/data/rtpmidid/bin/avahi-daemon" /etc/systemd/system/avahi-daemon.service ||
  fail "--force-avahi did not replace the unit"
[ -f "$DOMAIN/avahi-daemon.service.orig" ] || fail "no backup of the replaced unit"
sh "$PKG/install.sh" --uninstall -y >/dev/null 2>&1 || fail "uninstall failed"
grep -q "Description=stub pre-existing avahi" /etc/systemd/system/avahi-daemon.service ||
  fail "uninstall did not restore the original avahi unit"
rm -f /etc/systemd/system/avahi-daemon.service
ok "--force-avahi replaces the unit and uninstall restores it"

# --- 8. a device with no avahi unit at all (stock MPC OS) -----------------
FAKE_AVAHI_UNIT=0 sh "$PKG/install.sh" -n -y --etc-units >/tmp/noavahi.log 2>&1 ||
  {
    cat /tmp/noavahi.log
    fail "install without any avahi unit failed"
  }
[ -f "$ETCUNIT/avahi-mpc.service" ] || fail "no bundled avahi unit"
[ -L "$ETCUNIT/multi-user.target.wants/avahi-mpc.service" ] || fail "bundled avahi unit not enabled"
ok "install provides its own avahi unit when the device has none"

# --- 9. an avahi that is really installed is left alone -------------------
printf '#!/bin/sh\nexit 0\n' >/usr/sbin/avahi-daemon
chmod 755 /usr/sbin/avahi-daemon
sh "$PKG/install.sh" -n -y --etc-units >/tmp/haveavahi.log 2>&1 || fail "install failed"
grep -q "left alone" /tmp/haveavahi.log || fail "did not report reusing the installed avahi"
if [ -f "$ETCUNIT/avahi-daemon.service.d/50-rtpmidid-mpc.conf" ]; then
  fail "added a drop-in although avahi is installed"
fi
if [ -f "$ETCUNIT/avahi-mpc.service" ]; then fail "installed a second avahi"; fi
rm -f /usr/sbin/avahi-daemon
ok "leaves an avahi that is already installed alone"

# --- 10. read-only /etc: the overlay upperdir path ------------------------
# Two overlays on /etc, as on a device with user access enabled. /etc is
# writable in this container, so a dangling symlink at the write-probe path is
# what makes the probe fail.
cat >/tmp/mountinfo <<'EOF'
41 18 0:27 / /etc rw,relatime shared:38 - overlay overlay rw,lowerdir=/etc,upperdir=/tmp/etc-upper,workdir=/tmp/etc-work,uuid=null
96 41 0:49 / /etc rw,relatime shared:343 - overlay overlay ro,lowerdir=/x:/etc,redirect_dir=on
EOF
[ "$(MOUNTINFO=/tmp/mountinfo sh "$PKG/install.sh" --print-etc-upperdir)" = /tmp/etc-upper ] ||
  fail "--print-etc-upperdir did not find upperdir in the super options"
mkdir -p /tmp/etc-upper
ln -sfn /nonexistent/.rtpmidid-write-test /etc/systemd/system/.rtpmidid-write-test
sed -i '/^avahi:/d' /etc/passwd /etc/group
FAKE_UNIT_INVISIBLE=1 MOUNTINFO=/tmp/mountinfo sh "$PKG/install.sh" -n -y --etc-units >/tmp/roetc.log 2>&1 ||
  {
    cat /tmp/roetc.log
    fail "install with a read-only /etc failed"
  }
[ -f /tmp/etc-upper/systemd/system/rtpmidid.service ] ||
  fail "unit not written into the overlay upperdir"
[ -L /tmp/etc-upper/systemd/system/multi-user.target.wants/rtpmidid.service ] ||
  fail "unit not enabled in the overlay upperdir"
grep -q '^avahi:' "$DOMAIN/etc/avahi-user/passwd" ||
  fail "no private passwd although /etc cannot gain an account"
grep -q "BindReadOnlyPaths=/data/rtpmidid/etc/avahi-user/passwd:/etc/passwd" \
  /tmp/etc-upper/systemd/system/avahi-daemon.service.d/50-rtpmidid-mpc.conf ||
  fail "the avahi unit does not bind the private passwd"
grep -q "Reboot to activate" /tmp/roetc.log || fail "no reboot hint"
grep -q "do NOT autostart" /tmp/roetc.log || fail "no warning about overlay-only units"
rm -f /etc/systemd/system/.rtpmidid-write-test
rm -rf /tmp/etc-upper
ok "read-only /etc: units in the upperdir, private passwd for avahi, reboot announced"

# --- 11. a leftover rtpmidid.service is taken over and restored -----------
sh "$PKG/uninstall.sh" -y >/dev/null 2>&1 || fail "uninstall failed"
printf '[Unit]\nDescription=stub leftover rtpmidid\n[Service]\nExecStart=/sbin/rtpmidid\n' \
  >/etc/systemd/system/rtpmidid.service
sh "$PKG/install.sh" -n -y >/tmp/takeover.log 2>&1 ||
  {
    cat /tmp/takeover.log
    fail "install over a leftover rtpmidid.service failed"
  }
grep -q "moved aside" /tmp/takeover.log || fail "no message about the leftover unit"
if [ -e /etc/systemd/system/rtpmidid.service ]; then
  fail "the leftover /etc unit would shadow the rootfs one"
fi
grep -q "ExecStart=/data/rtpmidid/bin/rtpmidid" "$ROOTFS/rtpmidid.service" ||
  fail "the rootfs unit is not ours"
[ -f "$DOMAIN/rtpmidid.service.orig" ] || fail "no backup of the leftover unit"
sh "$PKG/uninstall.sh" -y >/dev/null 2>&1 || fail "uninstall after a takeover failed"
grep -q "ExecStart=/sbin/rtpmidid" /etc/systemd/system/rtpmidid.service ||
  fail "the leftover unit was not restored on uninstall"
rm -f /etc/systemd/system/rtpmidid.service
ok "a leftover rtpmidid.service is moved aside and restored"

# --- 12. -t installs elsewhere --------------------------------------------
sh "$PKG/install.sh" -n -y -t /data/rtpmidid-alt >/tmp/alt.log 2>&1 ||
  {
    cat /tmp/alt.log
    fail "install -t failed"
  }
[ -x /data/rtpmidid-alt/bin/rtpmidid ] || fail "-t target not used"
grep -q "ExecStart=/data/rtpmidid-alt/bin/rtpmidid" "$ROOTFS/rtpmidid.service" ||
  fail "-t not in the unit"
sh "$PKG/uninstall.sh" -y -t /data/rtpmidid-alt >/tmp/alt-rm.log 2>&1 ||
  {
    cat /tmp/alt-rm.log
    fail "uninstall -t failed"
  }
if [ -e /data/rtpmidid-alt ]; then fail "-t target left behind"; fi
ok "-t installs and removes at a custom prefix"

# --- 13. uninstall, from both locations ----------------------------------
sh "$PKG/install.sh" -n -y >/tmp/install3.log 2>&1 || fail "final install failed"
sh "$PKG/uninstall.sh" -y >/tmp/rm.log 2>&1 ||
  {
    cat /tmp/rm.log
    fail "uninstall.sh wrapper failed"
  }
[ -e "$DOMAIN" ] && fail "payload left behind"
for f in "$ROOTFS/rtpmidid.service" "$ROOTFS/avahi-mpc.service" \
  "$ROOTFS/multi-user.target.wants/rtpmidid.service" \
  "$ETCUNIT/rtpmidid.service" "$ETCUNIT/avahi-mpc.service" \
  "$ETCUNIT/avahi-daemon.service.d/50-rtpmidid-mpc.conf"; do
  [ -e "$f" ] && fail "left behind: $f"
done
if [ -f /etc/dbus-1/system.d/avahi-dbus.conf ]; then
  fail "left behind the D-Bus policy this installer added"
fi
grep -q '^avahi:' /etc/passwd || fail "uninstall removed the avahi account"
ok "uninstall removes payload and units from both places, keeps the account"

echo "==> all checks passed for $MPC_ARCH"
INNER
