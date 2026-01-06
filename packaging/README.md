# Docker-based Debian Package Builder

This directory contains the Docker-based build system for creating Debian packages across multiple distributions and architectures.

## Usage

From the project root:

```bash
# Build for default (Debian Trixie, amd64)
make docker-deb

# Build for specific distribution and architecture
make docker-deb DISTRO=ubuntu-24.04 ARCH=arm64

# Build for all distributions and architectures
make docker-deb-all
```

## Supported Distributions

- `debian-trixie` - Debian Trixie
- `ubuntu-24.04` - Ubuntu 24.04 LTS
- `ubuntu-25.10` - Ubuntu 25.10

## Supported Architectures

- `amd64` - x86_64
- `arm64` - ARM 64-bit
- `armhf` - ARM 32-bit (ARMv7)

## Output

Built packages are extracted to `releases/<distro>/<arch>/` directory.

## Adding New Distributions

To add a new distribution:

1. Create a new Dockerfile in `docker/` directory: `Dockerfile.<distro-name>`
2. Add the distro name to the `DISTROS` variable in `packaging/Makefile`
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
WORKDIR /build
```

## Requirements

- Docker (with buildx support recommended for multi-arch builds)
- QEMU emulation (automatically handled by Docker for cross-arch builds)

