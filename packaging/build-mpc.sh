#!/bin/bash
# Build the rtpmidid daemon for Akai MPC standalone devices and assemble the
# device package. Runs inside Docker (packaging/docker/Dockerfile.mpc); the
# source tree is mounted at /source and the result goes to /output:
#
#   /output/rtpmidid-<version>-mpc-<armv7|aarch64>.zip
#
# Same shape as the MPC plugin packages: a zip holding one folder with an
# install.sh, a MODES file (a zip loses the exec bits, and loses symlinks if it
# went through Windows) and SHA256SUMS. The MPC firmware has no package manager,
# so there is no .deb/.rpm. Only the daemon is packaged: no rtpmidid-cli, no
# shared library, no man pages.
#
# The compiled payload (the daemon plus the avahi files taken from the base
# image) is cached in $MPC_CACHE and keyed by a hash of everything that affects
# it. Editing install.sh, the units or the docs therefore costs a repack, not a
# cross build: nothing here recompiles unless a source file changed.
set -e

PREFIX=/data/rtpmidid
OUT=${OUTPUT_DIR:-/output}
SRC=/source
MPC_SRC=$SRC/packaging/mpc
CACHE=${MPC_CACHE:-}

# Bump when the way the payload is put together changes (not the sources).
CACHE_VERSION=1

# What the device provides; anything else the binaries need is shipped in
# payload/lib and loaded through LD_LIBRARY_PATH from the unit files.
# Verified with ldd on an MPC One (az0x 5.0.18, glibc 2.39):
#   libasound.so.2 1.2.11, libdbus-1.so.3, libexpat.so.1, libcap.so.2,
#   libsystemd.so.0, libstdc++.so.6 (GLIBCXX_3.4.32).
# libdbus-1 is deliberately not shipped: the device's copy is what the running
# dbus-daemon speaks to, and pulling Debian's would drag in its
# libsystemd/libgcrypt chain for no gain.
provided_by_device() {
  case "$(basename "$1")" in
    linux-vdso.so.1 | ld-linux*.so.* | \
      libc.so.6 | libm.so.6 | libdl.so.2 | libpthread.so.0 | librt.so.1 | \
      libgcc_s.so.1 | libstdc++.so.6 | \
      libasound.so.2 | libdbus-1.so.3 | libexpat.so.1 | libcap.so.2 | \
      libsystemd.so.0)
      return 0
      ;;
  esac
  return 1
}

log() { echo "==> $*"; }
die() {
  echo "ERROR: $*" >&2
  exit 1
}

needed_of() {
  objdump -p "$1" | awk '/^ *NEEDED/ {print $2}'
}
resolve() { # soname -> path in this container
  ldconfig -p | awk -v n="$1" '$1 == n { print $NF; exit }'
}

# --------------------------------------------------------------------------
# Architecture and version
# --------------------------------------------------------------------------
# The container architecture is the package architecture (the Makefile picks it
# with --platform). Debian says armhf/arm64, the MPC world says armv7/aarch64
# (Gen1 / Gen2 hardware), and the package is named after the latter.
DEB_ARCH=$(dpkg --print-architecture)
case "$DEB_ARCH" in
  armhf) MPC_ARCH=armv7 ;;
  arm64) MPC_ARCH=aarch64 ;;
  *) die "unsupported architecture '$DEB_ARCH' (expected armhf or arm64)" ;;
esac
log "package architecture: $MPC_ARCH (Debian $DEB_ARCH)"

VERSION=${RTPMIDID_VERSION:-}
if [ -z "$VERSION" ]; then
  VERSION=$(git -C "$SRC" describe --match 'v[0-9]*' --tags --abbrev=5 HEAD 2>/dev/null |
    sed -e 's/^v//' -e 's/-/~/g') || true
fi
[ -n "$VERSION" ] || die "cannot determine the version (pass RTPMIDID_VERSION)"
log "version: $VERSION"

# --------------------------------------------------------------------------
# Is the cached payload still valid?
# --------------------------------------------------------------------------
source_key() {
  local dep_versions
  dep_versions=$(dpkg-query -W -f='${Package} ${Version}\n' avahi-daemon libfmt9 2>/dev/null |
    LC_ALL=C sort | tr '\n' ' ')
  {
    echo "$CACHE_VERSION $DEB_ARCH $dep_versions"
    (cd "$SRC" && find CMakeLists.txt src lib include third_party -type f 2>/dev/null |
      LC_ALL=C sort | xargs sha256sum)
  } | sha256sum | cut -c1-16
}

KEY=$(source_key)
PAYLOAD=
if [ -n "$CACHE" ] && [ -f "$CACHE/STAMP" ] && [ -f "$CACHE/payload/bin/rtpmidid" ] &&
  [ "$(cat "$CACHE/STAMP" 2>/dev/null)" = "$KEY" ]; then
  PAYLOAD=$CACHE/payload
  log "sources unchanged (payload key $KEY): reusing the compiled payload"
fi

# --------------------------------------------------------------------------
# Compile the daemon and collect the libraries the device does not have
# --------------------------------------------------------------------------
check_payload() { # $1 = payload directory
  local obj soname missing=0
  for obj in "$1"/bin/* "$1"/lib/*; do
    for soname in $(needed_of "$obj"); do
      provided_by_device "$soname" && continue
      if [ ! -f "$1/lib/$soname" ]; then
        echo "missing dependency: $soname (needed by $obj)" >&2
        missing=1
      fi
    done
  done
  [ $missing = 0 ] || die "the payload is missing libraries the device does not have"
}

max_version() { # $1 = prefix (GLIBC_ / GLIBCXX_)
  objdump -T "$1"/bin/* "$1"/lib/* 2>/dev/null |
    grep -o "$2[0-9]\+\(\.[0-9]\+\)*" | sed "s/^$2//" | sort -u -V | tail -n 1
}
check_max() { # $1 = name, $2 = max found, $3 = limit on the device
  [ -n "$2" ] || die "cannot determine the $1 version of the payload"
  awk -v n="$1" -v v="$2" -v l="$3" 'BEGIN {
      n1 = split(v, a, "."); n2 = split(l, b, ".")
      m = (n1 > n2) ? n1 : n2
      for (i = 1; i <= m; i++) {
        x = a[i] + 0; y = b[i] + 0
        if (x > y) { bad = 1; break }
        if (x < y) break
      }
      printf "%s max %s (device has %s)\n", n, v, l
      exit bad
    }' || die "MPC OS 3 ships an older $1 than this build needs"
}

build_payload() { # $1 = destination payload directory
  local dest=$1

  BUILD=/tmp/rtpmidid-build
  rm -rf "$BUILD"
  mkdir -p "$BUILD"
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --exclude build --exclude 'packaging/dist' "$SRC/" "$BUILD/"
  else
    cp -a "$SRC/." "$BUILD/"
    rm -rf "$BUILD/build" "$BUILD/packaging/dist"
  fi
  cd "$BUILD"

  # C++17 + libfmt: bookworm's GCC 12 has no <format>, and the device does not
  # care as long as the libc it links against is old enough. libstdc++ stays
  # dynamic on purpose (the device's GLIBCXX_3.4.32 is newer than bookworm's
  # 3.4.30), so libfmt/libavahi are the only libraries shipped.
  cmake -S . -B build-mpc -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TESTS=OFF \
    -DENABLE_PCH=OFF \
    -DLDD=system \
    -DCPP_VERSION=17 \
    -DUSE_FMT=ON
  cmake --build build-mpc --target rtpmidid2-daemon

  local daemon=build-mpc/src/rtpmidid
  [ -x "$daemon" ] || die "the build did not produce $daemon"

  rm -rf "$dest"
  mkdir -p "$dest/bin" "$dest/lib"
  install -m 755 "$daemon" "$dest/bin/rtpmidid"
  install -m 755 /usr/sbin/avahi-daemon "$dest/bin/avahi-daemon"

  # Walk DT_NEEDED instead of using ldd on purpose: ldd is transitive, so it
  # follows the dependencies of libdbus-1/libsystemd, which we take from the
  # device, and would ship copies of libgcrypt/liblzma/libzstd that the device's
  # own libsystemd does not use.
  local queue="" seen="" soname src
  for bin in "$dest"/bin/*; do queue="$queue $(needed_of "$bin")"; done
  while [ -n "$queue" ]; do
    # shellcheck disable=SC2086 # word splitting is the queue
    set -- $queue
    soname=$1
    shift
    queue=$*
    case " $seen " in *" $soname "*) continue ;; esac
    seen="$seen $soname"
    provided_by_device "$soname" && continue
    src=$(resolve "$soname")
    [ -n "$src" ] || die "cannot resolve $soname in the build container"
    cp -L "$src" "$dest/lib/"
    queue="$queue $(needed_of "$src")"
  done

  strip --strip-unneeded "$dest"/bin/* "$dest"/lib/* 2>/dev/null || true

  # Point the binaries at their own libraries so they also run when started by
  # hand for debugging, not only under the unit's LD_LIBRARY_PATH. Each shipped
  # library gets $ORIGIN too: RUNPATH is not inherited by dependencies, so
  # libavahi-client needs its own to find libavahi-common next to it.
  patchelf --set-rpath '$ORIGIN/../lib' "$dest"/bin/*
  patchelf --set-rpath '$ORIGIN' "$dest"/lib/*
}

if [ -z "$PAYLOAD" ]; then
  if [ -n "$CACHE" ]; then DEST=$CACHE/payload; else DEST=/tmp/mpc-payload; fi
  log "building the daemon for $MPC_ARCH"
  build_payload "$DEST"
  PAYLOAD=$DEST
  if [ -n "$CACHE" ]; then
    if echo "$KEY" >"$CACHE/STAMP" 2>/dev/null; then
      log "payload cached under $CACHE (key $KEY)"
    else
      log "warning: cannot write the payload cache in $CACHE, rebuilding next time"
    fi
  fi
fi

check_payload "$PAYLOAD"
check_max GLIBC "$(max_version "$PAYLOAD" GLIBC_)" 2.39
check_max GLIBCXX "$(max_version "$PAYLOAD" GLIBCXX_)" 3.4.32

# --------------------------------------------------------------------------
# Stage the package
# --------------------------------------------------------------------------
PKG=$OUT/rtpmidid-$VERSION-mpc-$MPC_ARCH
rm -rf "$PKG"
mkdir -p "$PKG/payload/bin" "$PKG/payload/lib" "$PKG/payload/etc" \
  "$PKG/payload/services"

cp -a "$PAYLOAD/bin/." "$PKG/payload/bin/"
cp -a "$PAYLOAD/lib/." "$PKG/payload/lib/"
install -m 644 "$MPC_SRC/rtpmidid.ini" "$PKG/payload/etc/rtpmidid.ini"
install -m 644 "$MPC_SRC/avahi-daemon.conf" "$PKG/payload/etc/avahi-daemon.conf"
cp -a "$MPC_SRC/services/." "$PKG/payload/services/"

# aconnect: the MPC has aplaymidi/arecordmidi but not this one, and it is what
# you want from a shell to see and wire ALSA sequencer ports. Staged here (not in
# the cached payload) since it is just a copy from the image; it needs nothing the
# device does not already have (libasound.so.2, libc, libm).
install -m 755 /usr/bin/aconnect "$PKG/payload/bin/aconnect"

install -m 755 "$MPC_SRC/install.sh" "$PKG/install.sh"
install -m 755 "$MPC_SRC/uninstall.sh" "$PKG/uninstall.sh"
install -m 644 "$MPC_SRC/INSTALL.md" "$PKG/INSTALL.md"
echo "$VERSION" >"$PKG/VERSION"
echo "$MPC_ARCH" >"$PKG/ARCH"

# Same file the MPC plugin packages ship: a zip does not carry the exec bit, and
# loses symlinks if it is unpacked on Windows, so the things that need it are
# listed here (tab separated: kind, path[, target]) and restored by install.sh.
# 'x' = executable, 'l' = symlink.
(
  cd "$PKG"
  printf 'x\tinstall.sh\n'
  printf 'x\tuninstall.sh\n'
  find payload/bin -type f -printf 'x\t%p\n' | sort
) >"$PKG/MODES"

(
  cd "$PKG"
  find . -type f ! -name SHA256SUMS -print | sed 's|^\./||' | sort |
    xargs sha256sum >SHA256SUMS
)

ZIP=$OUT/rtpmidid-$VERSION-mpc-$MPC_ARCH.zip
rm -f "$ZIP"
(cd "$OUT" && zip -q -r "$(basename "$ZIP")" "$(basename "$PKG")")

chmod 777 "$OUT" 2>/dev/null || true
chown -R builder:builder "$OUT" 2>/dev/null || true

log "payload:"
(cd "$PKG/payload" && find . -type f | sort | while read -r f; do
  printf '    %8s  %s\n' "$(stat -c %s "$f")" "$f"
done)
log "zip: $ZIP ($(stat -c %s "$ZIP") bytes)"
log "install on the device (developer mode / user access enabled):"
echo "    scp $(basename "$ZIP") root@<mpc>:/data/"
echo "    ssh root@<mpc> 'cd /data && unzip -q $(basename "$ZIP") && sh $(basename "$PKG")/install.sh'"
