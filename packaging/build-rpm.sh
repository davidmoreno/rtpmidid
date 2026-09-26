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

# Get the version. The host passes it in RTPMIDID_VERSION because git describe
# inside the container is unreliable: the mounted source may be a git worktree
# whose .git file points outside the mount, or be owned by another uid. Only
# fall back to git here, and fail loudly instead of generating an empty
# "Version:" tag (rpmbuild then dies with "Empty tag: Version:").
VERSION="${RTPMIDID_VERSION:-}"
if [ -z "$VERSION" ]; then
    VERSION=$(git describe --match "v[0-9]*" --tags --abbrev=5 HEAD 2>/dev/null | sed 's/^v//g' | sed 's/-/~/g')
fi
if [ -z "$VERSION" ]; then
    echo "ERROR: cannot determine version: RTPMIDID_VERSION is unset and git describe failed" >&2
    exit 1
fi
echo "Building version $VERSION"

# Create rpmbuild directory structure
mkdir -p ~/rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

# Create source tarball
# Exclude build trees: 'build' matches build/ anywhere, './build-*' additionally
# catches build-asan/ and friends (anchored, so packaging/build*.sh is kept).
tar czf ~/rpmbuild/SOURCES/rtpmidid-${VERSION}.tar.gz \
    --exclude='.git' \
    --exclude='build' \
    --exclude='./build-*' \
    --exclude='packaging/dist' \
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
grep -q "^Version:        ${VERSION}$" rtpmidid.spec || {
    echo "ERROR: failed to set Version: ${VERSION} in the spec file" >&2
    exit 1
}
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
