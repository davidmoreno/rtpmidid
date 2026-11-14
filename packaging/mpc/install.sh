#!/bin/sh
# rtpmidid for Akai MPC standalone devices.
#
# Installs the daemon and starts it at boot, or removes it again:
#
#   sh install.sh [-t DIR] [-y] [-n] [--force-avahi]
#   sh install.sh --uninstall [-t DIR] [-y]
#
# Options:
#   -t, --target DIR   install prefix (default /data/rtpmidid)
#   -y, --yes          answer yes to every question (no prompts)
#   -n, --no-mpc       do not touch the MPC application (its ALSA ports appear
#                      after the next MPC start)
#       --uninstall    remove what this package installed
#       --reinstall    overwrite everything this package installed, including an
#                      rtpmidid.ini you edited, without asking
#       --etc-units     keep the unit files in the /etc overlay instead of the
#                      firmware rootfs. They then do NOT start at boot (systemd
#                      does not see overlay-only units when it starts); use this
#                      only if you start the daemon yourself.
#       --force-avahi  replace the firmware's avahi-daemon unit with ours
#                      instead of adding a drop-in next to it. Useful when the
#                      firmware's unit has an ExecStart we cannot live with.
#       --print-etc-upperdir
#                      print where the /etc overlay's writable upperdir is and
#                      exit (diagnostic, changes nothing)
#   -h, --help         this text
#
# Nothing outside the target and the systemd unit files is touched: no MPC
# settings, no firmware partition. The daemon needs no MPC-side configuration
# because it only speaks to the ALSA sequencer, and the MPC lists sequencer
# clients as MIDI inputs.
set -e
umask 022

HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"

TARGET=/data/rtpmidid
# Filled in by prepare_avahi_user(): the BindReadOnlyPaths lines that give our
# avahi-daemon the "avahi" user it insists on. Empty when the device has one.
AVAHI_USER_BIND=
YES=0
NO_MPC=0
DO_UNINSTALL=0
REINSTALL=0
ROOTFS_UNITS=1

FORCE_AVAHI=0
PRINT_ETC_UPPERDIR=0

usage() {
  cat <<'EOF'
Install or remove rtpmidid on an Akai MPC standalone device.

  sh install.sh [-t DIR] [-y] [-n] [--force-avahi]
  sh install.sh --uninstall [-t DIR] [-y]

Options:
  -t, --target DIR   install prefix (default /data/rtpmidid)
  -y, --yes          answer yes to every question (no prompts)
  -n, --no-mpc       do not touch the MPC application (its ALSA ports appear
                     after the next MPC start)
      --uninstall    remove what this package installed
      --reinstall    overwrite everything this package installed, including an
                     rtpmidid.ini you edited, without asking
      --etc-units    keep the units in the /etc overlay instead of the firmware
                     rootfs. Units there do NOT start at boot (systemd does not
                     see overlay-only units when it starts): only use this if you
                     start the daemon yourself. The default writes them into
                     /usr/lib/systemd/system, remounting / read-write briefly.
      --force-avahi  replace the firmware's avahi-daemon unit with ours instead
                     of adding a drop-in next to it
      --print-etc-upperdir
                     print where the /etc overlay's writable upperdir is and
                     exit (diagnostic, changes nothing)
  -h, --help         this text

Run it from the unpacked package directory, as root.
EOF
}

die() {
  echo "error: $*" >&2
  exit 1
}

# yes/no question; -y turns every one of them into "yes"
confirm() {
  [ $YES = 1 ] && return 0
  printf "%s [y/N] " "$1"
  read -r answer || answer=
  case "$answer" in
    y | Y | yes) return 0 ;;
  esac
  return 1
}

while [ $# -gt 0 ]; do
  case "$1" in
    -t | -p | --target | --prefix)
      [ -n "${2:-}" ] || die "$1 needs a directory"
      TARGET=$2
      shift 2
      ;;
    -y | --yes) YES=1; shift ;;
    -n | --no-mpc) NO_MPC=1; shift ;;
    --uninstall) DO_UNINSTALL=1; shift ;;
    --reinstall) REINSTALL=1; shift ;;
    --rootfs-units) ROOTFS_UNITS=1; shift ;;
    --etc-units) ROOTFS_UNITS=0; shift ;;
    --force-avahi) FORCE_AVAHI=1; shift ;;
    --print-etc-upperdir) PRINT_ETC_UPPERDIR=1; shift ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      die "unknown option: $1"
      ;;
  esac
done
case "$TARGET" in /*) ;; *) die "-t/--target must be an absolute path" ;; esac

# Where unit files can go. With "user access" enabled /etc is a read-only
# overlay whose writable upperdir lives on /data; that upperdir is where the
# firmware itself keeps its units (including an rtpmidid.service and an enabled
# avahi-daemon unit that have no binaries to run). Writing there is the only way
# to add a unit without touching the read-only root filesystem.
# upperdir of the overlay mounted on /etc, if any. In mountinfo the overlay
# options are one comma separated field after the "-" (fstype, source, super
# options), so upperdir= has to be split out of that field; a device can have two
# overlays on /etc (a writable one and a read-only one stacked on top), so all
# the /etc lines are tried.
etc_upperdir() {
  awk '
    {
      sep = 0
      for (i = 7; i <= NF; i++) if ($i == "-") { sep = i; break }
      if (!sep || $5 != "/etc" || $(sep + 1) != "overlay") next
      opts = $(sep + 3)
      for (i = sep + 4; i <= NF; i++) opts = opts " " $i
      n = split(opts, opt, ",")
      for (i = 1; i <= n; i++)
        if (opt[i] ~ /^upperdir=/) {
          sub(/^upperdir=/, "", opt[i])
          print opt[i]
          exit
        }
    }' "${MOUNTINFO:-/proc/self/mountinfo}"
}

# The firmware rootfs, read-only since boot: remount rw around the writes.
rootfs_rw() {
  if touch /usr/lib/systemd/system/.rtpmidid-rw-test 2>/dev/null; then
    rm -f /usr/lib/systemd/system/.rtpmidid-rw-test
    return 0
  fi
  mount -o remount,rw / 2>/dev/null ||
    die "cannot remount / read-write to write the unit files"
  ROOTFS_REMOUNTED=1
  touch /usr/lib/systemd/system/.rtpmidid-rw-test 2>/dev/null ||
    die "/ is read-write but /usr/lib/systemd/system is not writable"
  rm -f /usr/lib/systemd/system/.rtpmidid-rw-test
}

rootfs_ro() {
  [ "${ROOTFS_REMOUNTED:-0}" = 1 ] || return 0
  sync
  mount -o remount,ro / && ROOTFS_REMOUNTED=0
}

detect_systemd_dir() {
  # Is /etc itself writable? Used for the avahi account and the D-Bus policy,
  # independent of where the units go.
  if touch /etc/systemd/system/.rtpmidid-write-test 2>/dev/null; then
    rm -f /etc/systemd/system/.rtpmidid-write-test
    ETC_WRITABLE=1
  else
    ETC_WRITABLE=0
  fi
  UPPER=$(etc_upperdir)
  # Where units would live on the /etc side, so they can be cleaned up when they
  # move to the rootfs (whichever of the two is actually writable).
  if [ $ETC_WRITABLE = 1 ]; then
    ETC_UNIT_DIR=/etc/systemd/system
  elif [ -n "$UPPER" ]; then
    ETC_UNIT_DIR=$UPPER/systemd/system
  else
    ETC_UNIT_DIR=
  fi

  if [ $ROOTFS_UNITS = 1 ]; then
    # systemd does not see units that only exist in the /etc overlay when it
    # starts, so an overlay-only unit never autostarts on these devices (the
    # firmware's own overlay units, like its avahi one, do not either). The
    # rootfs is visible from the first moment, and it is where the firmware
    # keeps everything it starts at boot.
    rootfs_rw
    SYSTEMD_DIR=/usr/lib/systemd/system
    echo "  units go to $SYSTEMD_DIR (firmware rootfs, autostarts)"
  elif [ $ETC_WRITABLE = 1 ]; then
    SYSTEMD_DIR=/etc/systemd/system
    echo "  /etc is writable, units go to $SYSTEMD_DIR"
  else
    if [ -z "$UPPER" ] && [ -d /data/system/etc/overlay/systemd/system ]; then
      # Last resort for this image: the layout /data/system/etc/overlay is what
      # its etc.mount unit uses, even when mountinfo cannot tell us.
      UPPER=/data/system/etc/overlay
    fi
    [ -n "$UPPER" ] ||
      die "/etc is read-only and its overlay upperdir is not in /proc/self/mountinfo
       (run 'sh install.sh --print-etc-upperdir' and report the output)"
    SYSTEMD_DIR=$UPPER/systemd/system
    echo "  /etc is read-only (user access is enabled)"
    echo "  units go to $SYSTEMD_DIR"
    echo "  NOTE: units that only exist there do NOT autostart; drop --etc-units to"
    echo "  install them into the firmware rootfs instead"
  fi
  mkdir -p "$SYSTEMD_DIR/multi-user.target.wants" ||
    die "cannot write to $SYSTEMD_DIR"
}

mpc_service() {
  if systemctl cat acvs.service >/dev/null 2>&1; then
    echo acvs.service
  elif systemctl cat inmusic-mpc.service >/dev/null 2>&1; then
    echo inmusic-mpc.service
  fi
}

# The MPC application enumerates ALSA ports when it starts, so restart it to make
# the daemon's ports show up as MIDI inputs.
restart_mpc() {
  [ $NO_MPC = 1 ] && return 0
  service=$(mpc_service)
  [ -n "$service" ] || return 0
  if ! confirm "The MPC ($service) will be restarted so it picks up the new MIDI ports. Save your project first. Continue?"; then
    echo "  skipped: restart the MPC yourself later (systemctl restart $service)"
    return 0
  fi
  systemctl restart "$service" || echo "  warning: could not restart $service"
}

# ------------------------------------------------------------------ remove ----
remove_units() { # $1 = systemd unit directory
  rm -f "$1/rtpmidid.service" \
    "$1/multi-user.target.wants/rtpmidid.service" \
    "$1/rtpmidid-mpc.service" \
    "$1/multi-user.target.wants/rtpmidid-mpc.service" \
    "$1/avahi-mpc.service" \
    "$1/multi-user.target.wants/avahi-mpc.service" \
    "$1/avahi-daemon.service.d/50-rtpmidid-mpc.conf"
  rmdir "$1/avahi-daemon.service.d" 2>/dev/null || true
}

do_uninstall() {
  echo "Removing rtpmidid from $TARGET"
  [ "$(id -u)" = 0 ] || die "run as root"
  command -v systemctl >/dev/null || die "systemctl not found: this is not a systemd device"
  # Uninstall looks at the /etc side (where the firmware's own units live, and
  # what has to be restored); the rootfs copies are handled below, guided by the
  # marker a --rootfs-units install leaves.
  ROOTFS_UNITS=0
  detect_systemd_dir

  if [ -d "$TARGET" ] && ! confirm "Remove $TARGET and the systemd units?"; then
    die "cancelled"
  fi

  systemctl stop rtpmidid.service 2>/dev/null || true

  # The D-Bus policy is only there if a previous install added it.
  if [ -f "$TARGET/.dbus-policy-added" ]; then
    if [ $ETC_WRITABLE = 1 ]; then
      rm -f /etc/dbus-1/system.d/avahi-dbus.conf
    else
      rm -f "$UPPER/dbus-1/system.d/avahi-dbus.conf"
    fi
    systemctl reload dbus.service 2>/dev/null || true
  fi

  # Restore the firmware's avahi unit if we replaced it.
  if [ -f "$TARGET/.avahi-replaced" ]; then
    if [ -f "$TARGET/avahi-daemon.service.orig" ]; then
      cp "$TARGET/avahi-daemon.service.orig" "$SYSTEMD_DIR/avahi-daemon.service"
      chmod 644 "$SYSTEMD_DIR/avahi-daemon.service"
      echo "  restored the firmware's avahi-daemon.service"
    else
      rm -f "$SYSTEMD_DIR/avahi-daemon.service"
    fi
  fi

  remove_units "$SYSTEMD_DIR"
  # A previous --rootfs-units install put copies in the firmware rootfs.
  if [ -f "$TARGET/.rootfs-units" ]; then
    rootfs_rw
    remove_units /usr/lib/systemd/system
    rootfs_ro
  fi

  # A package version that tried a VST2 autostart plugin left it on a drive.
  if [ -f "$TARGET/.autostart-plugin" ]; then
    PLUGIN_DIR=$(cat "$TARGET/.autostart-plugin")
    case "$PLUGIN_DIR" in
      */"VST2 Plugins/rtpmidid - VST - Autostart")
        rm -rf -- "$PLUGIN_DIR"
        echo "  removed the old autostart plugin from $PLUGIN_DIR"
        ;;
      *) echo "  warning: not removing unexpected plugin path '$PLUGIN_DIR'" ;;
    esac
  fi
  if [ -f "$TARGET/.rtpmidid-unit-replaced" ] && [ -n "$ETC_UNIT_DIR" ]; then
    if [ -f "$TARGET/rtpmidid.service.orig" ]; then
      mkdir -p "$ETC_UNIT_DIR"
      cp "$TARGET/rtpmidid.service.orig" "$ETC_UNIT_DIR/rtpmidid.service"
      echo "  restored the rtpmidid.service that was there before"
    fi
  fi

  if [ -d "$TARGET" ]; then
    rm -rf "$TARGET"
  fi

  systemctl daemon-reload || true
  systemctl reset-failed avahi-daemon.service 2>/dev/null || true

  echo "Removed $TARGET and the units it installed"
  if [ $ETC_WRITABLE = 0 ]; then
    echo "The unit files were removed from $SYSTEMD_DIR; they disappear from"
    echo "/etc after the next reboot."
  fi
}

# Debugging aid: telling a user to run this is easier than reading mountinfo.
if [ $PRINT_ETC_UPPERDIR = 1 ]; then
  upper=$(etc_upperdir)
  echo "${upper:-<no upperdir found for /etc>}"
  exit 0
fi

if [ $DO_UNINSTALL = 1 ]; then
  do_uninstall
  exit 0
fi

# ----------------------------------------------------------------- install ----
echo "Installing rtpmidid $(cat VERSION 2>/dev/null) for MPC ($(cat ARCH 2>/dev/null))"

[ "$(id -u)" = 0 ] || die "run as root (this is an MPC developer-mode install)"
[ -c /dev/snd/seq ] || die "/dev/snd/seq not found: no ALSA sequencer on this device"
command -v systemctl >/dev/null || die "systemctl not found: this is not a systemd device"

PKG_ARCH=$(cat ARCH 2>/dev/null || echo unknown)
case "$PKG_ARCH:$(uname -m)" in
  armv7:armv7*) ;;
  aarch64:aarch64) ;;
  armv7:aarch64)
    die "this package is for Gen1 (32-bit ARM) devices and this one is aarch64: use the -mpc-aarch64 package"
    ;;
  aarch64:*)
    die "this package is for Gen2 (aarch64) devices and this one is $(uname -m): use the -mpc-armv7 package"
    ;;
  *) die "package is for $PKG_ARCH, this device is $(uname -m)" ;;
esac

if [ -f SHA256SUMS ]; then
  sha256sum -c SHA256SUMS >/dev/null 2>&1 ||
    die "package files are damaged (SHA256SUMS mismatch): copy it to the device again"
fi

detect_systemd_dir

if [ -d "$TARGET" ] && [ $REINSTALL = 0 ] &&
  ! confirm "$TARGET already exists (an installation). Replace it?"; then
  die "cancelled"
fi
if [ $REINSTALL = 1 ] && [ -d "$TARGET" ]; then
  echo "  --reinstall: overwriting $TARGET and everything in it"
fi
if [ ! -d "$TARGET" ] && [ -e "$SYSTEMD_DIR/rtpmidid.service" ] &&
  ! confirm "$SYSTEMD_DIR/rtpmidid.service already exists. Overwrite it?"; then
  die "cancelled"
fi

# The name rtpmidid.service may already belong to somebody else -- a dead unit
# from an earlier hand install (this MPC has one, ExecStart=/sbin/rtpmidid), or a
# unit of another rtpmidid package. An /etc-side unit outranks the rootfs one, so
# move it aside first; uninstall puts it back.
if [ -n "$ETC_UNIT_DIR" ] && [ -f "$ETC_UNIT_DIR/rtpmidid.service" ] &&
  ! grep -q "ExecStart=$TARGET/bin/rtpmidid" "$ETC_UNIT_DIR/rtpmidid.service"; then
  mkdir -p "$TARGET"
  if [ ! -f "$TARGET/rtpmidid.service.orig" ]; then
    cp "$ETC_UNIT_DIR/rtpmidid.service" "$TARGET/rtpmidid.service.orig"
  fi
  : >"$TARGET/.rtpmidid-unit-replaced"
  echo "  rtpmidid.service already existed (not ours): moved aside, restored on uninstall"
  rm -f "$ETC_UNIT_DIR/rtpmidid.service"
fi
systemctl stop rtpmidid.service 2>/dev/null || true
# from a package version that used the -mpc suffix
systemctl stop rtpmidid-mpc.service 2>/dev/null || true

# ---------------------------------------------------------------- payload ----
echo "  payload -> $TARGET"
rm -rf "$TARGET.new"
mkdir -p "$TARGET.new"
cp -a payload/. "$TARGET.new/"

# A zip does not carry the exec bit, and loses symlinks if it was unpacked on
# Windows: MODES, the same file the MPC plugin packages ship, lists what to put
# back (tab separated: kind, path[, link target]). The chmods after it are the
# fallback for a package without the file.
TAB=$(printf '\t')
if [ -f MODES ]; then
  while IFS=$TAB read -r kind rel link_target; do
    case "$rel" in "" | /* | .. | ../* | */..) continue ;; esac
    case "$kind" in
      x)
        if [ -f "$TARGET.new/$rel" ]; then chmod 755 "$TARGET.new/$rel"; fi
        ;;
      l)
        [ -n "${link_target:-}" ] || continue
        rm -f "$TARGET.new/$rel"
        ln -s "$link_target" "$TARGET.new/$rel"
        ;;
    esac
  done <MODES
fi
chmod 755 "$TARGET.new"/bin/* 2>/dev/null || true
chmod 644 "$TARGET.new"/lib/* "$TARGET.new"/etc/* "$TARGET.new"/services/* \
  2>/dev/null || true

# ini handling: keep one that was edited by hand, but do apply changed defaults.
# The copy this package ships is always kept next to it as rtpmidid.ini.dist, so
# "edited" means "differs from what the previous install shipped" -- comparing
# against today's default instead would silently never deliver a new default.
SHIPPED=$TARGET.new/etc/rtpmidid.ini
cp -a "$SHIPPED" "$TARGET.new/etc/rtpmidid.ini.dist"
if [ $REINSTALL = 0 ] && [ -f "$TARGET/etc/rtpmidid.ini" ]; then
  if [ -f "$TARGET/etc/rtpmidid.ini.dist" ] &&
    cmp -s "$TARGET/etc/rtpmidid.ini" "$TARGET/etc/rtpmidid.ini.dist"; then
    echo "  updated $TARGET/etc/rtpmidid.ini (not edited since the last install)"
  elif ! cmp -s "$TARGET/etc/rtpmidid.ini" "$SHIPPED"; then
    cp -a "$TARGET/etc/rtpmidid.ini" "$SHIPPED"
    echo "  kept your $TARGET/etc/rtpmidid.ini"
    echo "  (the defaults this package ships are in etc/rtpmidid.ini.dist; diff them)"
  fi
fi

# State from a previous install (what we changed outside the target) has to
# survive the swap, otherwise uninstall cannot undo it.
for marker in .dbus-policy-added .avahi-replaced avahi-daemon.service.orig \
  .rtpmidid-unit-replaced rtpmidid.service.orig; do
  if [ -f "$TARGET/$marker" ]; then cp -a "$TARGET/$marker" "$TARGET.new/$marker"; fi
done

rm -rf "$TARGET.old"
if [ -d "$TARGET" ]; then mv "$TARGET" "$TARGET.old"; fi
mv "$TARGET.new" "$TARGET"
rm -rf "$TARGET.old"

# The binary has to run here: this catches a package built for the wrong libc, or
# against libraries the device does not have.
if ! OUT=$("$TARGET/bin/rtpmidid" --version 2>&1); then
  echo "$OUT" >&2
  die "the daemon does not run on this device (see the error above)"
fi
echo "  $OUT"

# ----------------------------------------------------------------- units ----
# Render a unit template: @PREFIX@ becomes the install prefix, and
# @AVAHI_USER_BIND@ is replaced by the BindReadOnlyPaths lines (or dropped) --
# see prepare_avahi_user below.
render() { # $1 = template in $TARGET/services, $2 = destination
  sed "s|@PREFIX@|$TARGET|g" "$TARGET/services/$1" >"$2.rendering"
  awk -v add="$AVAHI_USER_BIND" '
    /@AVAHI_USER_BIND@/ { if (add != "") print add; next }
    { print }' "$2.rendering" >"$2"
  rm -f "$2.rendering"
  chmod 644 "$2"
}

# Delete something this installer created. It refuses any path outside the
# install prefix: the system's own /etc/passwd, /etc/group and every other file
# outside $TARGET are never removed or written by this script.
remove_ours() {
  case "$1" in
    "$TARGET"/*) rm -rf -- "$1" ;;
    *) die "refusing to remove $1: outside $TARGET" ;;
  esac
}

# avahi-daemon looks up its own user ("avahi") on startup and refuses to run
# without it, even with --no-drop-root. The MPC firmware has no avahi at all, so
# there is no such user, /etc is read-only and there is no useradd. Editing
# /etc/passwd through the overlay would freeze that file at install time and mask
# later firmware changes, so instead that one unit gets its own passwd and group
# -- copies kept under $TARGET/etc/avahi-user, with the avahi entry appended --
# bind-mounted read-only inside that unit's mount namespace. No file under /etc
# is written, and the mount is read-only and private to avahi-daemon.
first_free_id() { # $1 = file, $2 = field number, from 999 down
  id=999
  while [ $id -gt 99 ] &&
    awk -F: -v f="$2" -v id="$id" '$f == id { found = 1 } END { exit !found }' "$1"; do
    id=$((id - 1))
  done
  echo $id
}

AVAHI_USER_DIR=$TARGET/etc/avahi-user

# Create the account with whatever this device has -- busybox adduser, Debian's
# adduser or useradd -- and decide by looking at /etc/passwd rather than by
# trusting an exit code. Returns 1 if /etc/passwd did not gain the entry (the
# usual reason: /etc is read-only).
create_avahi_account() {
  nologin=/bin/false
  [ -x /usr/sbin/nologin ] && nologin=/usr/sbin/nologin
  if command -v adduser >/dev/null 2>&1; then
    adduser --system --no-create-home --group avahi >/dev/null 2>&1 || true
    grep -q '^avahi:' /etc/passwd && return 0
    adduser -S -D -H -h /run/avahi-daemon -s "$nologin" avahi >/dev/null 2>&1 || true
    grep -q '^avahi:' /etc/passwd && return 0
  fi
  if command -v useradd >/dev/null 2>&1; then
    useradd -r -M -s "$nologin" avahi >/dev/null 2>&1 || true
    grep -q '^avahi:' /etc/passwd && return 0
  fi
  return 1
}

prepare_avahi_user() {
  AVAHI_USER_BIND=
  if grep -q '^avahi:' /etc/passwd 2>/dev/null ||
    { command -v getent >/dev/null 2>&1 && getent passwd avahi >/dev/null 2>&1; }; then
    remove_ours "$AVAHI_USER_DIR"
    return 0
  fi
  # A writable /etc gets a normal account, and it is never removed again: an
  # account other things may reference should not vanish behind the system's
  # back (see the uninstall section).
  if [ $ETC_WRITABLE = 1 ]; then
    if create_avahi_account; then
      echo "  created the \"avahi\" account with adduser (kept on uninstall)"
      remove_ours "$AVAHI_USER_DIR"
      return 0
    fi
    echo "  warning: could not create the \"avahi\" account, using a private passwd"
  fi
  uid=$(first_free_id /etc/passwd 3)
  gid=$(first_free_id /etc/group 3)
  mkdir -p "$AVAHI_USER_DIR"
  cp /etc/passwd "$AVAHI_USER_DIR/passwd"
  cp /etc/group "$AVAHI_USER_DIR/group"
  printf 'avahi:x:%s:%s:Avahi mDNS/DNS-SD:/run/avahi-daemon:/bin/false\n' \
    "$uid" "$gid" >>"$AVAHI_USER_DIR/passwd"
  printf 'avahi:x:%s:\n' "$gid" >>"$AVAHI_USER_DIR/group"
  chmod 644 "$AVAHI_USER_DIR/passwd" "$AVAHI_USER_DIR/group"
  AVAHI_USER_BIND="BindReadOnlyPaths=$AVAHI_USER_DIR/passwd:/etc/passwd
BindReadOnlyPaths=$AVAHI_USER_DIR/group:/etc/group"
  echo "  no \"avahi\" user here: avahi gets a private passwd (uid $uid, gid $gid)"
}

render rtpmidid.service.in "$SYSTEMD_DIR/rtpmidid.service"
if [ $ROOTFS_UNITS = 1 ]; then
  : >"$TARGET/.rootfs-units"
  # An /etc-side copy from an earlier install would shadow the rootfs one after a
  # daemon-reload; both come from the same template, so keep one of them.
  if [ -n "$ETC_UNIT_DIR" ]; then
    remove_units "$ETC_UNIT_DIR"
  fi
fi
ln -sf ../rtpmidid.service \
  "$SYSTEMD_DIR/multi-user.target.wants/rtpmidid.service"

# --- avahi -------------------------------------------------------------------
# rtpmidid needs an avahi-daemon on the D-Bus system bus to do mDNS. The MPC
# firmware does not ship one (no binaries, no units, not even an "avahi" user),
# so the payload carries avahi-daemon and libavahi. What we find on a device,
# politest first:
#   - --force-avahi: replace that unit name with ours anyway;
#   - an avahi that is really installed (a binary exists): leave it completely
#     alone, ours is not needed;
#   - an avahi-daemon.service whose binary is missing: that is a leftover from
#     somebody's earlier manual install (what this MPC has, from 2022), so point
#     the existing unit at our copy with a drop-in instead of adding a second
#     mDNS daemon next to it;
#   - nothing at all (a stock device): install avahi-mpc.service.
AVAHI_BIN=$(command -v avahi-daemon 2>/dev/null || true)
HAVE_AVAHI_UNIT=0
if systemctl cat avahi-daemon.service >/dev/null 2>&1; then HAVE_AVAHI_UNIT=1; fi

if [ $ROOTFS_UNITS = 1 ]; then
  # Our own unit on purpose: the firmware's (or a leftover) avahi-daemon.service
  # lives in the /etc overlay, which is not seen when systemd starts, so it would
  # never come up at boot.
  prepare_avahi_user
  AVAHI_UNIT=avahi-mpc.service
  render avahi-mpc.service.in "$SYSTEMD_DIR/avahi-mpc.service"
  ln -sf ../avahi-mpc.service \
    "$SYSTEMD_DIR/multi-user.target.wants/avahi-mpc.service"
  # An /etc-side copy from an earlier install shadows the rootfs one after a
  # daemon-reload, and its drop-in would start a second daemon if that unit is
  # ever started later.
  if [ -n "$ETC_UNIT_DIR" ]; then
    remove_units "$ETC_UNIT_DIR"
  fi
  echo "  mDNS through $AVAHI_UNIT (rootfs unit)"
elif [ $FORCE_AVAHI = 1 ]; then
  prepare_avahi_user
  if [ $HAVE_AVAHI_UNIT = 1 ] &&
    ! confirm "Replace the avahi-daemon.service on this device with this package's unit?"; then
    die "cancelled (drop --force-avahi to use a drop-in instead)"
  fi
  if [ -f "$SYSTEMD_DIR/avahi-daemon.service" ] &&
    [ ! -f "$TARGET/avahi-daemon.service.orig" ]; then
    cp "$SYSTEMD_DIR/avahi-daemon.service" "$TARGET/avahi-daemon.service.orig"
  fi
  : >"$TARGET/.avahi-replaced"
  render avahi-mpc.service.in "$SYSTEMD_DIR/avahi-daemon.service"
  rm -f "$SYSTEMD_DIR/avahi-daemon.service.d/50-rtpmidid-mpc.conf"
  rmdir "$SYSTEMD_DIR/avahi-daemon.service.d" 2>/dev/null || true
  rm -f "$SYSTEMD_DIR/avahi-mpc.service" \
    "$SYSTEMD_DIR/multi-user.target.wants/avahi-mpc.service"
  AVAHI_UNIT=avahi-daemon.service
  echo "  mDNS through $AVAHI_UNIT (replaced by this package)"
elif [ -n "$AVAHI_BIN" ]; then
  # Somebody already installed avahi properly: do not touch it, do not run a
  # second responder next to it, and undo our own leftovers from an earlier run.
  remove_ours "$AVAHI_USER_DIR"
  rm -f "$SYSTEMD_DIR/avahi-daemon.service.d/50-rtpmidid-mpc.conf"
  rmdir "$SYSTEMD_DIR/avahi-daemon.service.d" 2>/dev/null || true
  rm -f "$SYSTEMD_DIR/avahi-mpc.service" \
    "$SYSTEMD_DIR/multi-user.target.wants/avahi-mpc.service"
  if [ $HAVE_AVAHI_UNIT = 1 ]; then
    AVAHI_UNIT=avahi-daemon.service
    # A replaced unit would now point at ours; put the original back.
    if [ -f "$TARGET/.avahi-replaced" ] &&
      [ -f "$TARGET/avahi-daemon.service.orig" ]; then
      cp "$TARGET/avahi-daemon.service.orig" "$SYSTEMD_DIR/avahi-daemon.service"
    fi
    echo "  mDNS through the avahi already installed ($AVAHI_BIN), left alone"
  else
    AVAHI_UNIT=
    echo "  using the avahi already installed ($AVAHI_BIN), no unit to start"
  fi
elif [ $HAVE_AVAHI_UNIT = 1 ]; then
  prepare_avahi_user
  AVAHI_UNIT=avahi-daemon.service
  mkdir -p "$SYSTEMD_DIR/avahi-daemon.service.d"
  render avahi-daemon.service.d.conf.in \
    "$SYSTEMD_DIR/avahi-daemon.service.d/50-rtpmidid-mpc.conf"
  rm -f "$SYSTEMD_DIR/avahi-mpc.service" \
    "$SYSTEMD_DIR/multi-user.target.wants/avahi-mpc.service"
  if ! systemctl is-enabled avahi-daemon.service >/dev/null 2>&1; then
    ln -sf ../avahi-daemon.service \
      "$SYSTEMD_DIR/multi-user.target.wants/avahi-daemon.service"
  fi
  echo "  avahi-daemon.service exists here but avahi-daemon does not:"
  echo "  pointing that unit at the copy in this package (drop-in)"
else
  prepare_avahi_user
  AVAHI_UNIT=avahi-mpc.service
  render avahi-mpc.service.in "$SYSTEMD_DIR/avahi-mpc.service"
  ln -sf ../avahi-mpc.service \
    "$SYSTEMD_DIR/multi-user.target.wants/avahi-mpc.service"
  echo "  mDNS through $AVAHI_UNIT (this device has no avahi)"
fi

# ------------------------------------------------------------ D-Bus policy ----
# avahi has to be allowed to own org.freedesktop.Avahi on the system bus. The
# image ships that policy; add it when a firmware does not, and replace it only
# when asked (--force-avahi).
NEED_REBOOT=0
POLICY=/etc/dbus-1/system.d/avahi-dbus.conf
if [ $ETC_WRITABLE = 1 ]; then
  DBUS_POLICY_DIR=/etc/dbus-1/system.d
else
  DBUS_POLICY_DIR=$UPPER/dbus-1/system.d
fi
WANT_POLICY=0
if [ ! -f "$POLICY" ]; then
  WANT_POLICY=1
elif [ $FORCE_AVAHI = 1 ] && ! cmp -s "$TARGET/services/avahi-dbus.conf" "$POLICY" &&
  confirm "The firmware's avahi D-Bus policy differs from this package's. Replace it?"; then
  WANT_POLICY=1
fi
if [ $WANT_POLICY = 1 ]; then
  mkdir -p "$DBUS_POLICY_DIR"
  cp "$TARGET/services/avahi-dbus.conf" "$DBUS_POLICY_DIR/avahi-dbus.conf"
  : >"$TARGET/.dbus-policy-added"
  echo "  wrote the avahi D-Bus policy (missing or replaced)"
  if [ $ETC_WRITABLE = 1 ]; then
    systemctl reload dbus.service 2>/dev/null || true
  else
    NEED_REBOOT=1
  fi
fi

rootfs_ro

# ----------------------------------------------------------------- start ----
systemctl daemon-reload || die "systemctl daemon-reload failed"

if systemctl cat rtpmidid.service >/dev/null 2>&1; then
  if [ -n "$AVAHI_UNIT" ]; then
    systemctl reset-failed "$AVAHI_UNIT" 2>/dev/null || true
    systemctl restart "$AVAHI_UNIT" || echo "  warning: $AVAHI_UNIT did not start"
  fi
  systemctl restart rtpmidid.service ||
    die "rtpmidid did not start: journalctl -u rtpmidid.service -n 50 --no-pager"
  echo "  rtpmidid.service is running${AVAHI_UNIT:+ (mDNS: $AVAHI_UNIT)}"
else
  # systemd did not see the new files in the overlay upperdir.
  NEED_REBOOT=1
  echo "  the unit is only visible to systemd after the next reboot"
fi

# Pointless before a reboot: nothing would see the ports yet.
if [ $NEED_REBOOT = 0 ]; then restart_mpc; fi

# ---------------------------------------------------------------- summary ----
echo
echo "Installed in $TARGET, config at $TARGET/etc/rtpmidid.ini"
echo "  log:      journalctl -u rtpmidid.service -f"
echo "  status:   systemctl status rtpmidid.service${AVAHI_UNIT:+ $AVAHI_UNIT}"
echo "  alsa:     cat /proc/asound/seq/clients"
echo "  remove:   sh $HERE/install.sh --uninstall"
if [ $ROOTFS_UNITS = 1 ]; then
  echo "  units:    /usr/lib/systemd/system (firmware rootfs). A firmware update"
  echo "            removes them: run this installer again after updating."
fi
if [ $NEED_REBOOT = 1 ]; then
  echo
  echo "Reboot to activate: /etc is a read-only overlay here, so the unit files"
  echo "(and any D-Bus policy) written to its upperdir only appear after /etc is"
  echo "rebuilt at boot."
fi
