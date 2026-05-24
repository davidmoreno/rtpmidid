#!/bin/bash
set -e

# This script runs inside the Docker container to build the Debian package
# The source code is mounted at /source

export HOME=/tmp/rtpmidid-home
mkdir -p "$HOME"

# Fail before a long compile if the host-mounted output dir is not writable
mkdir -p /output
if ! touch /output/.writetest 2>/dev/null; then
    echo "ERROR: /output is not writable (uid=$(id -u) gid=$(id -g))."
    echo "       Run packaging make as the same user that owns the output mount,"
    echo "       or fix permissions on the host output directory."
    exit 1
fi
rm -f /output/.writetest

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
# Note: changelog should already be updated on the host before Docker build
# dpkg-buildpackage creates files in the parent directory ($BUILD_PARENT)
make deb

# The .deb files will be created in the parent directory ($BUILD_PARENT)
echo "Package artifacts in BUILD_PARENT ($BUILD_PARENT):"
ls -la $BUILD_PARENT/ 2>/dev/null || echo "BUILD_PARENT directory not found"

deb_count=0
shopt -s nullglob
for deb in "$BUILD_PARENT"/*.deb; do
    cp -v "$deb" /output/
    deb_count=$((deb_count + 1))
done
shopt -u nullglob

if [ "$deb_count" -eq 0 ]; then
    echo "ERROR: No .deb files found in $BUILD_PARENT"
    echo "Build may have failed or packages were created in an unexpected location"
    exit 1
fi

# Copy other package-related files (optional metadata)
for pattern in "*.dsc" "*.tar.xz" "*.tar.gz" "*.buildinfo" "*.changes"; do
    for f in "$BUILD_PARENT"/$pattern; do
        [ -f "$f" ] || continue
        cp -v "$f" /output/
    done
done

echo "Built packages in /output:"
ls -la /output/
