# Docker-based Package Builder

This directory contains the Docker-based build system for creating Debian and RPM packages across multiple distributions and architectures.

## Usage

From the project root:

```bash
# Build Debian packages for default (Debian Trixie, amd64)
make docker-deb

# Build for specific distribution and architecture
make docker-deb DISTRO=ubuntu-24.04 ARCH=arm64

# Build all Debian packages
make docker-deb-all

# Build RPM packages for Fedora 43
make docker-rpm DISTRO=fedora-43 ARCH=x86_64

# Build all RPM packages
make docker-rpm-all
```

## Supported Distributions

### Debian/Ubuntu (DEB packages)
- `debian-trixie` - Debian Trixie
- `ubuntu-24.04` - Ubuntu 24.04 LTS
- `ubuntu-25.10` - Ubuntu 25.10

### Fedora (RPM packages)
- `fedora-43` - Fedora 43

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

Built packages are extracted to `releases/<distro>/<arch>/` directory.

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

1. Create a new Dockerfile in `docker/` directory: `Dockerfile.<distro-name>`
2. Add the distro name to the `RPM_DISTROS` variable in `packaging/Makefile`
3. The Dockerfile should install RPM build dependencies

Example:
```dockerfile
FROM fedora:43
RUN dnf install -y \
    rpm-build \
    cmake \
    ninja-build \
    gcc-c++ \
    avahi-devel \
    alsa-lib-devel \
    python3 \
    pandoc \
    git \
    systemd-rpm-macros \
    && dnf clean all
RUN useradd -m -s /bin/bash builder
COPY ../build-rpm.sh /usr/local/bin/rtpmidid-build-rpm.sh
RUN chmod +x /usr/local/bin/rtpmidid-build-rpm.sh
WORKDIR /build
```

## Requirements

- Docker (with buildx support recommended for multi-arch builds)
- QEMU emulation (automatically handled by Docker for cross-arch builds)

