# Multi-stage build for rtpmidid - slim production image
# Using Debian Trixie for GCC 14+ with full C++20 support (no libfmt needed)
FROM debian:trixie-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    libavahi-client-dev \
    libasound2-dev \
    pkg-config \
    python3 \
    python3-minimal \
    && rm -rf /var/lib/apt/lists/*

# Copy source code
WORKDIR /build
COPY . .

# Build the project
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -GNinja && \
    ninja && \
    cd ../cli && python3 -m zipapp rtpmidid-cli.py -o ../build/rtpmidid-cli -p /usr/bin/python3

# Runtime stage - minimal image
FROM debian:trixie-slim

# Install only runtime dependencies
# Note: No libfmt needed - GCC 14+ has full C++20 std::format support
RUN apt-get update && apt-get install -y \
    libavahi-client3 \
    libasound2 \
    python3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create rtpmidid user (matches systemd service)
RUN useradd -r -s /bin/false -G audio rtpmidid

# Copy built binaries from builder
COPY --from=builder /build/build/src/rtpmidid /usr/bin/rtpmidid
COPY --from=builder /build/build/rtpmidid-cli /usr/bin/rtpmidid-cli
RUN chmod +x /usr/bin/rtpmidid /usr/bin/rtpmidid-cli

# Create runtime directory
RUN mkdir -p /var/run/rtpmidid && \
    chown rtpmidid:audio /var/run/rtpmidid

# Default configuration directory
RUN mkdir -p /etc/rtpmidid

# Switch to non-root user
USER rtpmidid

# Default command
ENTRYPOINT ["/usr/bin/rtpmidid"]
CMD ["--ini=/etc/rtpmidid/default.ini"]
