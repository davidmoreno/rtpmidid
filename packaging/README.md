# Docker-based Package Builder

This directory contains the Docker-based build system for creating Debian and RPM packages across multiple distributions and architectures.

## Usage

### From the project root

```bash
# Build all packages (DEB and RPM)
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

# Build all packages (both DEB and RPM)
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

## Output

Built packages are extracted to `packaging/dist/<distro>/<arch>/` directory.

- Debian packages: `*.deb` files
- RPM packages: `*.rpm` files (binary and source RPMs)

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
