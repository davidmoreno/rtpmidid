#!/bin/bash
set -e

# This script runs inside the Docker container to build the RPM package
# The source code is mounted at /source

# Create a writable copy of the source
BUILD_PARENT=/tmp/rtpmidid-build
BUILD_DIR=$BUILD_PARENT/rtpmidid
rm -rf $BUILD_PARENT
mkdir -p $BUILD_DIR

# Copy source to build directory
if command -v rsync >/dev/null 2>&1; then
    rsync -a /source/ $BUILD_DIR/ || cp -a /source/. $BUILD_DIR/
else
    cp -a /source/. $BUILD_DIR/
fi

# Change to build directory
cd $BUILD_DIR

# Get version from git or use default
VERSION=$(git describe --match "v[0-9]*" --tags --abbrev=5 HEAD 2>/dev/null | sed 's/^v//g' | sed 's/-/~/g' || echo "0.0.0")

# Create rpmbuild directory structure
mkdir -p ~/rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

# Create source tarball
tar czf ~/rpmbuild/SOURCES/rtpmidid-${VERSION}.tar.gz \
    --exclude='.git' \
    --exclude='build' \
    --exclude='releases' \
    --exclude='*.deb' \
    --exclude='*.rpm' \
    --exclude='packaging/docker' \
    --transform "s,^,rtpmidid-${VERSION}/," \
    .

# Copy spec file
cp packaging/rpm/rtpmidid.spec ~/rpmbuild/SPECS/

# Build the RPM
cd ~/rpmbuild/SPECS
# Replace version in spec file
sed -i "s/^Version:.*/Version:        ${VERSION}/" rtpmidid.spec
rpmbuild -ba rtpmidid.spec

# Copy RPM files to /output for extraction
mkdir -p /output
cp -a ~/rpmbuild/RPMS/*/*.rpm /output/ 2>/dev/null || true
cp -a ~/rpmbuild/SRPMS/*.rpm /output/ 2>/dev/null || true

# List what we found
echo "Built packages:"
ls -la /output/ 2>/dev/null || echo "No packages found in /output"

# Ensure proper permissions
chown -R builder:builder /output 2>/dev/null || true
