# Docker-based package builder
# Supports multiple distributions and architectures (Debian/Ubuntu and Fedora)

# Default values
# DISTRO/ARCH are deliberately empty: the deb/rpm entry points fill them in
# from the host (see `make rpm`) or from the defaults for the given package
# type, so `make rpm` on Fedora 44/x86_64 builds fedora-44/x86_64.
DISTRO ?=
ARCH ?=
RELEASES_DIR ?= $(PACKAGING_DIR)/dist

# Version of the source tree from git tags, e.g. v26.01-18-g84719 ->
# 26.01~18~g84719. Computed on the host on purpose: inside the container the
# checkout may be a git worktree whose .git file points outside the mounted
# source directory, and git refuses repositories owned by another uid.
RTPMIDID_VERSION = $(shell git -C $(SOURCE_DIR) describe --match 'v[0-9]*' --tags --abbrev=5 HEAD 2>/dev/null | sed -e 's/^v//' -e 's/-/~/g')

# Available distributions (Debian/Ubuntu)
DEB_DISTROS := debian-trixie debian-bookworm ubuntu-24.04 ubuntu-25.10

# Available distributions (Fedora/RPM)
RPM_DISTROS := fedora-43

# All distributions
DISTROS := $(DEB_DISTROS) $(RPM_DISTROS)

# Available architectures for Debian/Ubuntu
DEB_ARCHES := amd64 arm64 armhf riscv64

# Available architectures for Fedora (RPM uses different names)
RPM_ARCHES := x86_64 aarch64

# All architectures (for compatibility)
ARCHES := amd64 arm64 armhf riscv64 x86_64 aarch64

# Defaults for `make deb` / `make rpm` when DISTRO and/or ARCH are not given.
# Derived from the release lists above so there is a single place to bump them.
DEB_DEFAULT_DISTRO ?= $(firstword $(DEB_DISTROS))
DEB_DEFAULT_ARCH ?= amd64
RPM_DEFAULT_DISTRO ?= $(firstword $(RPM_DISTROS))
RPM_DEFAULT_ARCH ?= x86_64

# Host detection.
HOST_OS_ID = $(shell . /etc/os-release 2>/dev/null && echo $$ID)
HOST_OS_VERSION_ID = $(shell . /etc/os-release 2>/dev/null && echo $$VERSION_ID)
HOST_ARCH = $(shell uname -m)

# Map architecture to Docker platform
DOCKER_PLATFORM_amd64 := linux/amd64
DOCKER_PLATFORM_arm64 := linux/arm64
DOCKER_PLATFORM_armhf := linux/arm/v7
DOCKER_PLATFORM_riscv64 := linux/riscv64
DOCKER_PLATFORM_x86_64 := linux/amd64
DOCKER_PLATFORM_aarch64 := linux/arm64

# Get Docker platform for architecture
get_platform = $(DOCKER_PLATFORM_$(ARCH))

# Build script used inside the container. Selected by the internal target that
# runs, not by matching the distro name against a list: any fedora-<release>
# (including releases with no pinned Dockerfile) is an RPM build.
BUILD_SCRIPT =
_deb-internal: BUILD_SCRIPT = /usr/local/bin/rtpmidid-build.sh
_rpm-internal: BUILD_SCRIPT = /usr/local/bin/rtpmidid-build-rpm.sh

# Docker image name
IMAGE_NAME = rtpmidid-builder-$(DISTRO)-$(ARCH)
# Dockerfile for $(DISTRO): a pinned docker/Dockerfile.<distro> if one exists,
# otherwise the generic Fedora template, whose release comes from
# FEDORA_VERSION (fedora-44 -> --build-arg FEDORA_VERSION=44).
DOCKERFILE = $(if $(wildcard $(PACKAGING_DIR)/docker/Dockerfile.$(DISTRO)),docker/Dockerfile.$(DISTRO),$(if $(filter fedora-%,$(DISTRO)),docker/Dockerfile.fedora))
DOCKER_BUILD_ARGS = $(if $(filter fedora-%,$(DISTRO)),--build-arg FEDORA_VERSION=$(patsubst fedora-%,%,$(DISTRO)))

# Source directory (parent of packaging/)
SOURCE_DIR = $(shell dirname $(shell pwd))
PACKAGING_DIR = $(shell pwd)

.PHONY: help deb deb-all deb-% rpm rpm-all rpm-% all _collect-publish

help:
	@echo "Docker Package Builder (Debian/Ubuntu and Fedora)"
	@echo ""
	@echo "Usage:"
	@echo "  make deb [DISTRO=<distro>] [ARCH=<arch>]  # Build Debian packages"
	@echo "  make rpm [DISTRO=<distro>] [ARCH=<arch>]  # Build RPM packages"
	@echo "         (no args: the host Fedora release and architecture)"
	@echo "  make deb-all  # Build all Debian packages"
	@echo "  make rpm-all   # Build all RPM packages"
	@echo "  make all       # Build all packages"
	@echo ""
	@echo "Debian/Ubuntu distributions: $(DEB_DISTROS)"
	@echo "Fedora distributions: $(RPM_DISTROS)"
	@echo "Debian architectures: $(DEB_ARCHES)"
	@echo "Fedora architectures: $(RPM_ARCHES)"
	@echo ""
	@echo "Examples:"
	@echo "  make deb"
	@echo "  make deb DISTRO=ubuntu-24.04 ARCH=arm64"
	@echo "  make rpm DISTRO=fedora-43 ARCH=x86_64"
	@echo "  make deb-all"
	@echo "  make rpm-all"
	@echo "  make all"

# Default target - build for the default distro/arch unless told otherwise
deb:
	@$(MAKE) -f $(PACKAGING_DIR)/Makefile deb-$(if $(DISTRO),$(DISTRO),$(DEB_DEFAULT_DISTRO))-$(if $(ARCH),$(ARCH),$(DEB_DEFAULT_ARCH))

# Build for all distro/arch combinations
deb-all:
	@for distro in $(DEB_DISTROS); do \
		for arch in $(DEB_ARCHES); do \
			echo "Building for $$distro/$$arch..."; \
			$(MAKE) deb-$$distro-$$arch || true; \
		done; \
	done

# Generic target for any distro-arch combination
# Handle distro names with dots (e.g., ubuntu-24.04)
deb-%:
	@distro_arch=$(subst deb-,,$@); \
	if echo "$$distro_arch" | grep -q "ubuntu-"; then \
		distro=$$(echo $$distro_arch | sed 's/-amd64$$//' | sed 's/-arm64$$//' | sed 's/-armhf$$//'); \
		arch=$$(echo $$distro_arch | sed 's/^.*-//'); \
	else \
		distro=$$(echo $$distro_arch | cut -d- -f1-2); \
		arch=$$(echo $$distro_arch | cut -d- -f3-); \
	fi; \
	$(MAKE) -f $(PACKAGING_DIR)/Makefile _deb-internal DISTRO=$$distro ARCH=$$arch

# Internal target that does the actual work
_deb-internal:
	@echo "Building Debian package for $(DISTRO) on $(ARCH)..."
	@if [ ! -f $(PACKAGING_DIR)/$(DOCKERFILE) ]; then \
		echo "ERROR: no Dockerfile for $(DISTRO) (expected $(PACKAGING_DIR)/$(DOCKERFILE))"; \
		exit 1; \
	fi
	@mkdir -p $(RELEASES_DIR)/$(DISTRO)/$(ARCH)
	@echo "Updating changelog with version from git tags..."
	@# NOTE: .git can be a directory or a file (git worktrees/submodules), so use
	@# git rev-parse instead of testing for a directory.
	@if git -C $(SOURCE_DIR) rev-parse --git-dir >/dev/null 2>&1 && [ -f $(SOURCE_DIR)/debian/update-changelog.py ]; then \
		cd $(SOURCE_DIR) && \
		chmod +x debian/update-changelog.py 2>/dev/null || true && \
		if python3 debian/update-changelog.py 2>&1; then \
			echo "Changelog updated successfully"; \
			echo "Current changelog version: $$(head -1 debian/changelog)"; \
		else \
			echo "ERROR: Failed to update changelog from git tags"; \
			exit 1; \
		fi; \
	else \
		if ! git -C $(SOURCE_DIR) rev-parse --git-dir >/dev/null 2>&1; then \
			echo "ERROR: no git repository found at $(SOURCE_DIR) - cannot determine version from git tags"; \
			exit 1; \
		fi; \
		if [ ! -f $(SOURCE_DIR)/debian/update-changelog.py ]; then \
			echo "ERROR: debian/update-changelog.py not found"; \
			exit 1; \
		fi; \
	fi
	@platform=$(call get_platform); \
	if [ -z "$$platform" ]; then \
		echo "Error: Unknown architecture $(ARCH)"; \
		exit 1; \
	fi; \
	echo "Using Docker platform: $$platform"; \
	\
	# Build Docker image with buildx for multi-arch support \
	# Build context is packaging/ so build.sh is available \
	# Use multiarch builder if available, otherwise default \
	builder=$$(docker buildx ls 2>/dev/null | grep -q "multiarch" && echo "multiarch" || echo "default"); \
	echo "Using buildx builder: $$builder"; \
	docker buildx build \
		--builder $$builder \
		--platform $$platform \
		--tag $(IMAGE_NAME) \
		--file $(PACKAGING_DIR)/$(DOCKERFILE) $(DOCKER_BUILD_ARGS) \
		--load \
		$(PACKAGING_DIR)/ 2>&1 | grep -v "^#.*DONE\|^#.*CACHED" | tail -20 || \
	( \
		echo "Note: buildx failed, trying regular docker build..."; \
		docker build \
			--platform $$platform \
			--tag $(IMAGE_NAME) \
			--file $(PACKAGING_DIR)/$(DOCKERFILE) $(DOCKER_BUILD_ARGS) \
			$(PACKAGING_DIR)/ \
	); \
	\
	# Create output directory for packages \
	mkdir -p /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
	\
	# Run container to build package \
	# Mount source with :z flag for SELinux compatibility \
	docker run --rm \
		--platform $$platform \
		-v $(SOURCE_DIR):/source:z \
		-v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH):/output:z \
		-u builder \
		$(IMAGE_NAME) \
		$(BUILD_SCRIPT) || \
	( \
		echo "Trying with root user..."; \
		docker run --rm \
			--platform $$platform \
			-v $(SOURCE_DIR):/source:z \
			-v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH):/output:z \
			$(IMAGE_NAME) \
			$(BUILD_SCRIPT) \
	); \
	\
	# Copy package files to dist directory, and fail if there are none: the
	# build script can die before producing anything, and silently reporting
	# success hides that.
	if [ -d /tmp/rtpmidid-output-$(DISTRO)-$(ARCH) ] && ls /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.deb >/dev/null 2>&1; then \
		cp -v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.deb $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/; \
		rm -rf /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
		echo "Package built successfully: $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/"; \
	else \
		rm -rf /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
		echo "ERROR: no .deb files were produced, see the output above"; \
		exit 1; \
	fi

# RPM-specific targets
# `make rpm` builds for the machine it runs on: Fedora 44/x86_64 gives
# fedora-44/x86_64. Any other host falls back to
# $(RPM_DEFAULT_DISTRO)/$(RPM_DEFAULT_ARCH). Explicit DISTRO=/ARCH= win.
rpm:
	@distro="$(DISTRO)"; \
	if [ -z "$$distro" ]; then \
		if [ "$(HOST_OS_ID)" = "fedora" ] && [ -n "$(HOST_OS_VERSION_ID)" ]; then \
			distro="fedora-$(HOST_OS_VERSION_ID)"; \
		else \
			distro="$(RPM_DEFAULT_DISTRO)"; \
		fi; \
	fi; \
	case " $(DEB_DISTROS) " in \
		*" $$distro "*) \
			echo "Error: $$distro is a Debian/Ubuntu distribution, use 'make deb' instead"; \
			exit 1;; \
	esac; \
	arch="$(ARCH)"; \
	case "$$arch" in "") arch="$(HOST_ARCH)";; amd64) arch=x86_64;; arm64) arch=aarch64;; esac; \
	case " $(RPM_ARCHES) " in *" $$arch "*) ;; *) arch="$(RPM_DEFAULT_ARCH)";; esac; \
	echo "Building RPM package for $$distro on $$arch..."; \
	$(MAKE) rpm-$$distro-$$arch

rpm-all:
	@for distro in $(RPM_DISTROS); do \
		for arch in $(RPM_ARCHES); do \
			echo "Building for $$distro/$$arch..."; \
			$(MAKE) rpm-$$distro-$$arch || true; \
		done; \
	done

# Generic target for any RPM distro-arch combination
rpm-%:
	@distro_arch=$(subst rpm-,,$@); \
	distro=$$(echo $$distro_arch | sed 's/-x86_64$$//' | sed 's/-aarch64$$//'); \
	arch=$$(echo $$distro_arch | sed 's/^.*-//'); \
	$(MAKE) -f $(PACKAGING_DIR)/Makefile _rpm-internal DISTRO=$$distro ARCH=$$arch

# Internal target for RPM builds
_rpm-internal:
	@echo "Building RPM package for $(DISTRO) on $(ARCH)..."
	@if [ ! -f $(PACKAGING_DIR)/$(DOCKERFILE) ]; then \
		echo "ERROR: no Dockerfile for $(DISTRO) (expected $(PACKAGING_DIR)/$(DOCKERFILE))"; \
		exit 1; \
	fi
	@mkdir -p $(RELEASES_DIR)/$(DISTRO)/$(ARCH)
	@platform=$(call get_platform); \
	if [ -z "$$platform" ]; then \
		echo "Error: Unknown architecture $(ARCH)"; \
		exit 1; \
	fi; \
	echo "Using Docker platform: $$platform"; \
	\
	# Build Docker image with buildx for multi-arch support \
	builder=$$(docker buildx ls 2>/dev/null | grep -q "multiarch" && echo "multiarch" || echo "default"); \
	echo "Using buildx builder: $$builder"; \
	docker buildx build \
		--builder $$builder \
		--platform $$platform \
		--tag $(IMAGE_NAME) \
		--file $(PACKAGING_DIR)/$(DOCKERFILE) $(DOCKER_BUILD_ARGS) \
		--load \
		$(PACKAGING_DIR)/ 2>&1 | grep -v "^#.*DONE\|^#.*CACHED" | tail -20 || \
	( \
		echo "Note: buildx failed, trying regular docker build..."; \
		docker build \
			--platform $$platform \
			--tag $(IMAGE_NAME) \
			--file $(PACKAGING_DIR)/$(DOCKERFILE) $(DOCKER_BUILD_ARGS) \
			$(PACKAGING_DIR)/ \
	); \
	\
	# Create output directory for packages \
	mkdir -p /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
	\
	# Run container to build package \
	# RTPMIDID_VERSION comes from the host git; see the variable definition. \
	docker run --rm \
		--platform $$platform \
		-v $(SOURCE_DIR):/source:z \
		-v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH):/output:z \
		-e RTPMIDID_VERSION=$(RTPMIDID_VERSION) \
		-u builder \
		$(IMAGE_NAME) \
		$(BUILD_SCRIPT) || \
	( \
		echo "Trying with root user..."; \
		docker run --rm \
			--platform $$platform \
			-v $(SOURCE_DIR):/source:z \
			-v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH):/output:z \
			-e RTPMIDID_VERSION=$(RTPMIDID_VERSION) \
			$(IMAGE_NAME) \
			$(BUILD_SCRIPT) \
	); \
	\
	# Copy .rpm files to dist directory, and fail if there are none: the build
	# script can die before producing anything, and silently reporting success
	# hides that.
	if [ -d /tmp/rtpmidid-output-$(DISTRO)-$(ARCH) ] && ls /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.rpm >/dev/null 2>&1; then \
		cp -v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.rpm $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/; \
		rm -rf /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
		echo "Package built successfully: $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/"; \
	else \
		rm -rf /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
		echo "ERROR: no .rpm files were produced, see the output above"; \
		exit 1; \
	fi

# Build all packages (both DEB and RPM)
all: deb-all rpm-all _collect-publish

# Collect main rtpmidid packages for publishing
# Only collects the main package (rtpmidid), not librtpmidid0 or dev packages
# All packages are placed in a single directory with distro and arch in filename
_collect-publish:
	@echo "Collecting main rtpmidid packages for publishing..."
	@PUBLISH_DIR=$(PACKAGING_DIR)/publish; \
	mkdir -p $$PUBLISH_DIR; \
	\
	# Process DEB packages \
	for distro in $(DEB_DISTROS); do \
		for arch in $(DEB_ARCHES); do \
			dist_dir=$(RELEASES_DIR)/$$distro/$$arch; \
			if [ -d $$dist_dir ]; then \
				# Find main rtpmidid package (not librtpmidid0 or dbgsym) \
				main_pkg=$$(ls $$dist_dir/rtpmidid_*.deb 2>/dev/null | grep -v dbgsym | head -1); \
				if [ -n "$$main_pkg" ]; then \
					# Extract filename and rename: rtpmidid_<version>_<arch>.deb -> rtpmidid-<distro>-<arch>-<version>.deb \
					basename_pkg=$$(basename $$main_pkg); \
					# Remove .deb extension, then extract version (everything between rtpmidid_ and _<arch>) \
					without_ext=$$(echo $$basename_pkg | sed 's/\.deb$$//'); \
					# Extract version by removing rtpmidid_ prefix and _<arch> suffix \
					# Use a more robust pattern that escapes the arch properly \
					arch_pattern="_$$arch"; \
					version=$$(echo $$without_ext | sed 's/^rtpmidid_//' | sed "s/$$arch_pattern\$$//"); \
					# Convert dots to underscores in distro name for filename \
					distro_safe=$$(echo $$distro | sed 's/\./_/g'); \
					# Construct new filename: rtpmidid-<distro>-<arch>-<version>.deb \
					new_name="rtpmidid-$$distro_safe-$$arch-$$version.deb"; \
					cp -v $$main_pkg $$PUBLISH_DIR/$$new_name; \
					echo "  Collected: $$distro/$$arch -> $$new_name"; \
				fi; \
			fi; \
		done; \
	done; \
	\
	# Process RPM packages \
	for distro in $(RPM_DISTROS); do \
		for arch in $(RPM_ARCHES); do \
			dist_dir=$(RELEASES_DIR)/$$distro/$$arch; \
			if [ -d $$dist_dir ]; then \
				# Find main rtpmidid package (not libs, devel, or src) \
				main_pkg=$$(ls $$dist_dir/rtpmidid-*.rpm 2>/dev/null | grep -vE "\-libs\-|\-devel\-|\.src\.rpm" | head -1); \
				if [ -n "$$main_pkg" ]; then \
					# Extract filename and rename: rtpmidid-<version>-<release>.<arch>.rpm -> rtpmidid-<distro>-<arch>-<version>-<release>.rpm \
					basename_pkg=$$(basename $$main_pkg); \
					# Remove .rpm extension \
					without_ext=$$(echo $$basename_pkg | sed 's/\.rpm$$//'); \
					# Extract version-release by removing rtpmidid- prefix and .<arch> suffix \
					# Use a more robust pattern that escapes the arch properly \
					arch_pattern=".$$arch"; \
					version_release=$$(echo $$without_ext | sed 's/^rtpmidid-//' | sed "s/$$arch_pattern\$$//"); \
					# Convert dots to underscores in distro name for filename \
					distro_safe=$$(echo $$distro | sed 's/\./_/g'); \
					# Construct new filename: rtpmidid-<distro>-<arch>-<version>-<release>.rpm \
					new_name="rtpmidid-$$distro_safe-$$arch-$$version_release.rpm"; \
					cp -v $$main_pkg $$PUBLISH_DIR/$$new_name; \
					echo "  Collected: $$distro/$$arch -> $$new_name"; \
				fi; \
			fi; \
		done; \
	done; \
	\
	echo ""; \
	echo "Packages ready for publishing in: $$PUBLISH_DIR"; \
	echo "  All packages in single directory with distro and arch in filename"

clean:
	rm -rf $(RELEASES_DIR)
	rm -rf $(PUBLISH_DIR)