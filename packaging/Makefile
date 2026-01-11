# Docker-based package builder
# Supports multiple distributions and architectures (Debian/Ubuntu and Fedora)

# Default values
DISTRO ?= debian-trixie
ARCH ?= amd64
RELEASES_DIR ?= $(PACKAGING_DIR)/dist

# Available distributions (Debian/Ubuntu)
DEB_DISTROS := debian-trixie ubuntu-24.04 ubuntu-25.10

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

# Map architecture to Docker platform
DOCKER_PLATFORM_amd64 := linux/amd64
DOCKER_PLATFORM_arm64 := linux/arm64
DOCKER_PLATFORM_armhf := linux/arm/v7
DOCKER_PLATFORM_riscv64 := linux/riscv64
DOCKER_PLATFORM_x86_64 := linux/amd64
DOCKER_PLATFORM_aarch64 := linux/arm64

# Get Docker platform for architecture
get_platform = $(DOCKER_PLATFORM_$(ARCH))

# Check if distro is RPM-based
is_rpm = $(if $(filter $(RPM_DISTROS),$(DISTRO)),yes,no)

# Docker image name
IMAGE_NAME = rtpmidid-builder-$(DISTRO)-$(ARCH)
DOCKERFILE = docker/Dockerfile.$(DISTRO)

# Build script (different for RPM vs DEB)
BUILD_SCRIPT = $(if $(filter yes,$(call is_rpm)),/usr/local/bin/rtpmidid-build-rpm.sh,/usr/local/bin/rtpmidid-build.sh)

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

# Default target - build for default distro/arch
deb: deb-$(DISTRO)-$(ARCH)

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
	@mkdir -p $(RELEASES_DIR)/$(DISTRO)/$(ARCH)
	@echo "Updating changelog with version from git tags..."
	@if [ -d $(SOURCE_DIR)/.git ] && [ -f $(SOURCE_DIR)/debian/update-changelog.py ]; then \
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
		if [ ! -d $(SOURCE_DIR)/.git ]; then \
			echo "ERROR: .git directory not found - cannot determine version from git tags"; \
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
		--file $(PACKAGING_DIR)/$(DOCKERFILE) \
		--load \
		$(PACKAGING_DIR)/ 2>&1 | grep -v "^#.*DONE\|^#.*CACHED" | tail -20 || \
	( \
		echo "Note: buildx failed, trying regular docker build..."; \
		docker build \
			--platform $$platform \
			--tag $(IMAGE_NAME) \
			--file $(PACKAGING_DIR)/$(DOCKERFILE) \
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
	# Copy package files to dist directory \
	if [ -d /tmp/rtpmidid-output-$(DISTRO)-$(ARCH) ]; then \
		if [ "$(call is_rpm)" = "yes" ]; then \
			if ls /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.rpm 1>/dev/null 2>&1; then \
				cp -v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.rpm $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/; \
			else \
				echo "ERROR: No .rpm files found in output directory"; \
				exit 1; \
			fi; \
		else \
			if ls /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.deb 1>/dev/null 2>&1; then \
				cp -v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.deb $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/; \
			else \
				echo "ERROR: No .deb files found in output directory"; \
				exit 1; \
			fi; \
		fi; \
		rm -rf /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
	else \
		echo "ERROR: Output directory not found"; \
		exit 1; \
	fi; \
	\
	echo "Package built successfully: $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/"

# RPM-specific targets
# Default to fedora-43/x86_64 if DISTRO is not set or is a DEB distro
rpm:
	@if [ -z "$(DISTRO)" ] || echo "$(DEB_DISTROS)" | grep -q "$(DISTRO)"; then \
		$(MAKE) rpm-fedora-43-x86_64; \
	else \
		rpm_arch=$(if $(filter $(DEB_ARCHES),$(ARCH)),x86_64,$(or $(ARCH),x86_64)); \
		$(MAKE) rpm-$(DISTRO)-$$rpm_arch; \
	fi

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
		--file $(PACKAGING_DIR)/$(DOCKERFILE) \
		--load \
		$(PACKAGING_DIR)/ 2>&1 | grep -v "^#.*DONE\|^#.*CACHED" | tail -20 || \
	( \
		echo "Note: buildx failed, trying regular docker build..."; \
		docker build \
			--platform $$platform \
			--tag $(IMAGE_NAME) \
			--file $(PACKAGING_DIR)/$(DOCKERFILE) \
			$(PACKAGING_DIR)/ \
	); \
	\
	# Create output directory for packages \
	mkdir -p /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
	\
	# Run container to build package \
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
	# Copy .rpm files to dist directory \
	if [ -d /tmp/rtpmidid-output-$(DISTRO)-$(ARCH) ]; then \
		cp -v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.rpm $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/ 2>/dev/null || \
		echo "Warning: No .rpm files found in output"; \
		rm -rf /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
	else \
		echo "Error: Output directory not found"; \
	fi; \
	\
	echo "Package built successfully: $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/"

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