# Docker-based Package Builder

This directory contains the Docker-based build system for creating Debian and RPM packages across multiple distributions and architectures, and the zip package for Akai MPC standalone devices.

## Usage

### From the project root

```bash
# Build all packages (DEB, RPM and MPC)
make packages

# Or use the packaging Makefile directly
make -C packaging all
```

### From the packaging directory

```bash
# Build Debian packages for default (Debian Trixie, amd64)
make deb

# Build for specific distribution and architecture
make deb DISTRO=ubuntu-24.04 ARCH=arm64

# Build all Debian packages
make deb-all

# Build RPM packages for the machine we are running on (Fedora 44/x86_64
# builds fedora-44/x86_64). DISTRO/ARCH override the detection.
make rpm

# Build RPM packages for an explicit distribution and architecture
make rpm DISTRO=fedora-43 ARCH=x86_64

# Build all RPM packages
make rpm-all

# Build the Akai MPC standalone package for Gen1 hardware (armv7)
make mpc

# ... or for Gen2 hardware (aarch64)
make mpc ARCH=aarch64

# Build both MPC generations
make mpc-all

# Build all packages (DEB, RPM and MPC)
make all
```

## Supported Distributions

### Debian/Ubuntu (DEB packages)

- `debian-trixie` - Debian Trixie
- `debian-bookworm` - Debian Bookworm (uses libfmt instead of C++20 `<format>`)
- `ubuntu-24.04` - Ubuntu 24.04 LTS
- `ubuntu-25.10` - Ubuntu 25.10

### Fedora (RPM packages)

- `fedora-43` - Fedora 43 (the pinned release used by `make rpm-all`)

Any other `fedora-<release>` works too: RPM builds use the generic
`docker/Dockerfile.fedora` template with `--build-arg FEDORA_VERSION=<release>`,
so a new Fedora release does not need a new Dockerfile. `make rpm` with no
arguments picks the host release and architecture.

## Supported Architectures

### Debian/Ubuntu

- `amd64` - x86_64
- `arm64` - ARM 64-bit
- `armhf` - ARM 32-bit (ARMv7)
- `riscv64` - RISC-V 64-bit

### Fedora

- `x86_64` - x86_64
- `aarch64` - ARM 64-bit

### Akai MPC

- `armv7` - MPC Gen1 (MPC One/Live/X/Force, 32-bit ARMv7, hard float)
- `aarch64` - MPC Gen2 (Live III/XL and later, 64-bit)

## Akai MPC standalone devices

The MPC firmware is a Yocto image (`az0x`, systemd) with no dpkg/opkg/rpm, so
there is nothing for a package manager to install. The artifact is a zip with an
`install.sh` inside, the same shape the MPC plugin packages use (top-level folder,
`MODES`, `SHA256SUMS`), and the installed pieces are:

```
/data/rtpmidid/          payload: bin/rtpmidid, bin/avahi-daemon, lib/, etc/
/etc/systemd/system/     rtpmidid.service (+ an avahi drop-in or avahi-mpc.service)
```

Things that are specific to the device, and why the builder looks the way it
does:

- **Base image is Debian 12 `bookworm` on purpose.** MPC OS 3 ships glibc 2.39
  and GLIBCXX 3.4.32; bookworm (2.36 / 3.4.30) is the newest base that still
  loads there. Debian 13 / Ubuntu 24.10+ produce binaries the MPC rejects, and
  the build fails the submission if a symbol version goes past those limits.
- **`/data` is the only writable exec location.** SD cards and USB drives are
  mounted `noexec`, and `/` is a read-only ext4. The payload therefore goes to
  `/data/rtpmidid`, not to the drive the zip comes on.
- **Units have to live in the firmware rootfs.** `/etc` is a read-only overlay
  once "user access" is enabled (upperdir on `/data/system/etc/overlay`), and
  systemd does not see units that only exist there when it boots — the firmware's
  own 2022-era avahi units in that overlay never start either. So `install.sh`
  writes `rtpmidid.service` and `avahi-mpc.service` into
  `/usr/lib/systemd/system`, remounting `/` read-write for about a second (the
  rootfs is plain ext4, no verity). `--etc-units` keeps the old overlay
  behaviour for anyone who starts the daemon by hand.
- **No avahi in the firmware**, so `avahi-daemon` and `libavahi-*`/`libdaemon`
  are shipped in the payload and run with `--no-drop-root --no-chroot` (there is
  no `avahi` user and `/etc` cannot be edited). `libasound`, `libdbus-1`,
  `libexpat`, `libcap`, `libstdc++` come from the device. Checked on an MPC One:
  the rootfs has no avahi unit, no D-Bus policy, no libraries and no `avahi`
  user; an `avahi-daemon.service` seen under `/etc` is a leftover from somebody
  installing avahi by hand (the one on the test device is from 2022), so
  `install.sh` treats the four cases separately: `--force-avahi` takes the unit
  name over, a binary that exists is left alone, a unit whose binary is missing
  gets a drop-in to our copy, and a device with nothing gets `avahi-mpc.service`.
- **avahi-daemon needs a root process and a resolvable `avahi` account** (the
  name is compiled in; running it unprivileged is refused outright, so `daemon`
  or `nobody` are not an option). With a writable `/etc` the installer creates
  the account with `adduser` and never removes it; with `/etc` read-only (user
  access enabled) it cannot, so the unit gets a private `passwd`/`group` under
  `/data/rtpmidid/etc/avahi-user` bind-mounted read-only into that one unit,
  and nothing under `/etc` is written.

The daemon is the only thing packaged: `rtpmidid-cli`, the shared library and the
man pages are not installed on an MPC.

- `docker/Dockerfile.mpc` - builder (arch comes from `--platform`)
- `build-mpc.sh` - runs in the container: builds `rtpmidid2-daemon`, resolves the
  libraries the device does not have, checks symbol versions, assembles the zip
- `mpc/install.sh` - the installer and remover (`--uninstall`), with `-t/--target`
  for the prefix, `-y/--yes` to skip the confirmation prompts (overwriting an
  existing install, restarting the MPC, replacing an existing avahi unit) and
  `--force-avahi` to replace that unit instead of adding a drop-in next to it;
  `mpc/uninstall.sh` is a one-line wrapper for the same code
- `mpc/rtpmidid.ini`, `mpc/avahi-daemon.conf`, `mpc/services/*`, `mpc/INSTALL.md`
  - what ends up in the zip; `INSTALL.md` is the end-user document

The `armv7` build is what has been run on hardware (MPC One, az0x 5.0.18);
`aarch64` uses the same code path and container recipe but has not been tried on
a Gen2 device.

The compiled payload is cached in `packaging/dist/.cache/mpc-<arch>/payload`,
keyed by a hash of the files that decide what goes into the binary
(`CMakeLists.txt`, `src/`, `lib/`, `include/`, `third_party/`) plus the base
image's avahi/fmt versions. So:

| what changed | what `make mpc` does | time |
|---|---|---|
| `install.sh`, units, ini, `INSTALL.md`, the Makefile | repacks the zip from the cached payload | ~3 s |
| anything in `src/`, `lib/`, `include/`, `third_party/`, `CMakeLists.txt` | rebuilds the daemon, re-caches, repacks | ~2-5 min (qemu) |
| `docker/Dockerfile.mpc` | the image rebuilds, the payload key changes if a shipped library changed | as above |

`make clean` (or deleting `packaging/dist/.cache`) forces a full rebuild.
`MPC_CACHE_VERSION` in `build-mpc.sh` is the escape hatch when the way the
payload is assembled changes but the sources do not.

`mpc/test-install.sh [armv7|aarch64]` runs `install.sh`/`uninstall.sh` inside a
container that has only the libraries an MPC has. It is a dry run, not a device
test, but it catches the mistakes that are expensive to debug on hardware: a
payload that needs `LD_LIBRARY_PATH`, a missing library, `@PREFIX@` left in a
unit, the wrong architecture being accepted, an upgrade losing the user's ini,
and uninstall leaving units behind.

## Output

Built packages are extracted to `packaging/dist/<distro>/<arch>/` directory.

- Debian packages: `*.deb` files
- RPM packages: `*.rpm` files (binary and source RPMs)
- MPC packages: `packaging/dist/mpc-<arch>/rtpmidid-<version>-mpc-<arch>.zip`
  (`_collect-publish` copies it to `packaging/publish/` with the same name, next
  to the debs and rpms)

## Adding New Distributions

### Adding a Debian/Ubuntu Distribution

1. Create a new Dockerfile in `docker/` directory: `Dockerfile.<distro-name>`
2. Add the distro name to the `DEB_DISTROS` variable in `packaging/Makefile`
3. The Dockerfile should install all build dependencies listed in `debian/control`

Example:

```dockerfile
FROM <distro>:<version>
RUN apt-get update && apt-get install -y \
    build-essential \
    debhelper \
    debhelper-compat \
    libavahi-client-dev \
    libasound2-dev \
    libfmt-dev \
    python3 \
    cmake \
    pandoc \
    git \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*
RUN useradd -m -s /bin/bash builder
COPY ../build.sh /usr/local/bin/rtpmidid-build.sh
RUN chmod +x /usr/local/bin/rtpmidid-build.sh
WORKDIR /build
```

### Adding a Fedora/RPM Distribution

Nothing to do for a new Fedora release: `docker/Dockerfile.fedora` is generic and
takes the release as `FEDORA_VERSION`. If a release needs different build
dependencies, add a pinned `docker/Dockerfile.<distro-name>` (it takes
precedence), and add the distro to `RPM_DISTROS` in `packaging/Makefile` if
`make rpm-all` should build it.

## Requirements

- Docker (with buildx support recommended for multi-arch builds)
- QEMU emulation (automatically handled by Docker for cross-arch builds)
