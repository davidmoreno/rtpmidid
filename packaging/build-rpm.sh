#!/bin/bash
set -e

# This script runs inside the Docker container to build the RPM package
# The source code is mounted at /source

export HOME=/tmp/rtpmidid-home
mkdir -p "$HOME"

mkdir -p /output
if ! touch /output/.writetest 2>/dev/null; then
    echo "ERROR: /output is not writable (uid=$(id -u) gid=$(id -g))."
    echo "       Run packaging make as the same user that owns the output mount,"
    echo "       or fix permissions on the host output directory."
    exit 1
fi
rm -f /output/.writetest

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
mkdir -p "$HOME/rpmbuild"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

# Create source tarball
tar czf "$HOME/rpmbuild/SOURCES/rtpmidid-${VERSION}.tar.gz" \
    --exclude='.git' \
    --exclude='build' \
    --exclude='packaging/dist' \
    --exclude='*.deb' \
    --exclude='*.rpm' \
    --exclude='packaging/docker' \
    --transform "s,^,rtpmidid-${VERSION}/," \
    .

# Copy spec file
cp packaging/rpm/rtpmidid.spec "$HOME/rpmbuild/SPECS/"

# Build the RPM
cd "$HOME/rpmbuild/SPECS"
# Replace version in spec file
sed -i "s/^Version:.*/Version:        ${VERSION}/" rtpmidid.spec
rpmbuild -ba rtpmidid.spec

# Copy RPM files to /output for extraction
rpm_count=0
shopt -s nullglob
for rpm in "$HOME/rpmbuild/RPMS"/*/*.rpm "$HOME/rpmbuild/SRPMS"/*.rpm; do
    cp -v "$rpm" /output/
    rpm_count=$((rpm_count + 1))
done
shopt -u nullglob

if [ "$rpm_count" -eq 0 ]; then
    echo "ERROR: No .rpm files found under $HOME/rpmbuild"
    exit 1
fi

echo "Built packages in /output:"
ls -la /output/
