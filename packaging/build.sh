#!/bin/bash
set -e

# This script runs inside the Docker container to build the Debian package
# The source code is mounted at /source

# Create a writable copy of the source
# dpkg-buildpackage expects to be run from the source directory
# and creates files in the parent directory
BUILD_PARENT=/tmp/rtpmidid-build
BUILD_DIR=$BUILD_PARENT/rtpmidid
rm -rf $BUILD_PARENT
mkdir -p $BUILD_DIR

# Copy source to build directory
# Try rsync first (preserves permissions best), fallback to cp
if command -v rsync >/dev/null 2>&1; then
    rsync -a /source/ $BUILD_DIR/ || cp -a /source/. $BUILD_DIR/
else
    cp -a /source/. $BUILD_DIR/
fi

# Change to build directory
cd $BUILD_DIR

# Run make deb which will execute dpkg-buildpackage
# dpkg-buildpackage creates files in the parent directory ($BUILD_PARENT)
make deb

# The .deb files will be created in the parent directory ($BUILD_PARENT)
# Copy them to /output for extraction
mkdir -p /output
cp -a $BUILD_PARENT/*.deb /output/ 2>/dev/null || true
cp -a $BUILD_PARENT/*.dsc /output/ 2>/dev/null || true
cp -a $BUILD_PARENT/*.tar.* /output/ 2>/dev/null || true
cp -a $BUILD_PARENT/*.buildinfo /output/ 2>/dev/null || true
cp -a $BUILD_PARENT/*.changes /output/ 2>/dev/null || true

# List what we found
echo "Built packages:"
ls -la /output/ 2>/dev/null || echo "No packages found in /output"

# Ensure proper permissions
chown -R builder:builder /output 2>/dev/null || true

