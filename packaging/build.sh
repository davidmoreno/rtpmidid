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
# Note: changelog should already be updated on the host before Docker build
# dpkg-buildpackage creates files in the parent directory ($BUILD_PARENT)
make deb

# The .deb files will be created in the parent directory ($BUILD_PARENT)
# Copy them to /output for extraction
mkdir -p /output
# Ensure /output is writable by the current user
chmod 777 /output 2>/dev/null || true

# Debug: List what was created in both BUILD_PARENT and current directory
echo "Files in BUILD_PARENT ($BUILD_PARENT):"
ls -la $BUILD_PARENT/ 2>/dev/null || echo "BUILD_PARENT directory not found"
echo "Files in current directory ($BUILD_DIR):"
ls -la $BUILD_DIR/*.deb 2>/dev/null || echo "No .deb files in current directory"

# Copy .deb files from parent directory (where dpkg-buildpackage creates them)
# Also check current directory just in case
deb_count=0
if find $BUILD_PARENT -maxdepth 1 -name "*.deb" -type f | grep -q .; then
    find $BUILD_PARENT -maxdepth 1 -name "*.deb" -type f -exec cp -v {} /output/ \;
    deb_count=$(find /output -maxdepth 1 -name "*.deb" -type f | wc -l)
elif find $BUILD_DIR -maxdepth 1 -name "*.deb" -type f | grep -q .; then
    find $BUILD_DIR -maxdepth 1 -name "*.deb" -type f -exec cp -v {} /output/ \;
    deb_count=$(find /output -maxdepth 1 -name "*.deb" -type f | wc -l)
else
    echo "ERROR: No .deb files found in $BUILD_PARENT or $BUILD_DIR"
    echo "Build may have failed or packages were created in an unexpected location"
    exit 1
fi

# Copy other package-related files
find $BUILD_PARENT -maxdepth 1 -name "*.dsc" -type f -exec cp -v {} /output/ \; 2>/dev/null || true
find $BUILD_PARENT -maxdepth 1 -name "*.tar.*" -type f -exec cp -v {} /output/ \; 2>/dev/null || true
find $BUILD_PARENT -maxdepth 1 -name "*.buildinfo" -type f -exec cp -v {} /output/ \; 2>/dev/null || true
find $BUILD_PARENT -maxdepth 1 -name "*.changes" -type f -exec cp -v {} /output/ \; 2>/dev/null || true

# Verify we have packages
echo "Built packages in /output:"
ls -la /output/ 2>/dev/null || true

# Fail if no .deb files were copied
if [ $deb_count -eq 0 ]; then
    echo "ERROR: No .deb files were copied to /output"
    echo "Expected packages in $BUILD_PARENT or $BUILD_DIR"
    exit 1
fi

# Ensure proper permissions
chown -R builder:builder /output 2>/dev/null || true

