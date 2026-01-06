# Docker-based package builder
# Supports multiple distributions and architectures (Debian/Ubuntu and Fedora)

# Default values
DISTRO ?= debian-trixie
ARCH ?= amd64
RELEASES_DIR ?= $(SOURCE_DIR)/releases

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

.PHONY: help docker-deb docker-deb-all docker-deb-% docker-rpm docker-rpm-all docker-rpm-%

help:
	@echo "Docker Package Builder (Debian/Ubuntu and Fedora)"
	@echo ""
	@echo "Usage:"
	@echo "  make docker-deb [DISTRO=<distro>] [ARCH=<arch>]  # Build Debian packages"
	@echo "  make docker-rpm [DISTRO=<distro>] [ARCH=<arch>]  # Build RPM packages"
	@echo "  make docker-deb-all  # Build all Debian packages"
	@echo "  make docker-rpm-all   # Build all RPM packages"
	@echo ""
	@echo "Debian/Ubuntu distributions: $(DEB_DISTROS)"
	@echo "Fedora distributions: $(RPM_DISTROS)"
	@echo "Debian architectures: $(DEB_ARCHES)"
	@echo "Fedora architectures: $(RPM_ARCHES)"
	@echo ""
	@echo "Examples:"
	@echo "  make docker-deb"
	@echo "  make docker-deb DISTRO=ubuntu-24.04 ARCH=arm64"
	@echo "  make docker-rpm DISTRO=fedora-43 ARCH=x86_64"
	@echo "  make docker-deb-all"
	@echo "  make docker-rpm-all"

# Default target - build for default distro/arch
docker-deb: docker-deb-$(DISTRO)-$(ARCH)

# Build for all distro/arch combinations
docker-deb-all:
	@for distro in $(DISTROS); do \
		for arch in $(ARCHES); do \
			echo "Building for $$distro/$$arch..."; \
			$(MAKE) docker-deb-$$distro-$$arch || true; \
		done; \
	done

# Generic target for any distro-arch combination
# Handle distro names with dots (e.g., ubuntu-24.04)
docker-deb-%:
	@distro_arch=$(subst docker-deb-,,$@); \
	if echo "$$distro_arch" | grep -q "ubuntu-"; then \
		distro=$$(echo $$distro_arch | sed 's/-amd64$$//' | sed 's/-arm64$$//' | sed 's/-armhf$$//'); \
		arch=$$(echo $$distro_arch | sed 's/^.*-//'); \
	else \
		distro=$$(echo $$distro_arch | cut -d- -f1-2); \
		arch=$$(echo $$distro_arch | cut -d- -f3-); \
	fi; \
	$(MAKE) -f $(PACKAGING_DIR)/Makefile _docker-deb-internal DISTRO=$$distro ARCH=$$arch

# Internal target that does the actual work
_docker-deb-internal:
	@echo "Building Debian package for $(DISTRO) on $(ARCH)..."
	@mkdir -p $(RELEASES_DIR)/$(DISTRO)/$(ARCH)
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
	# Copy package files to releases directory \
	if [ -d /tmp/rtpmidid-output-$(DISTRO)-$(ARCH) ]; then \
		if [ "$(call is_rpm)" = "yes" ]; then \
			cp -v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.rpm $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/ 2>/dev/null || \
			echo "Warning: No .rpm files found in output"; \
		else \
			cp -v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.deb $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/ 2>/dev/null || \
			echo "Warning: No .deb files found in output"; \
		fi; \
		rm -rf /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
	else \
		echo "Error: Output directory not found"; \
	fi; \
	\
	echo "Package built successfully: $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/"

# RPM-specific targets
docker-rpm: docker-rpm-$(or ${DISTRO},fedora-43)-$(or ${ARCH},x86_64)

docker-rpm-all:
	@for distro in $(RPM_DISTROS); do \
		for arch in $(RPM_ARCHES); do \
			echo "Building for $$distro/$$arch..."; \
			$(MAKE) docker-rpm-$$distro-$$arch || true; \
		done; \
	done

# Generic target for any RPM distro-arch combination
docker-rpm-%:
	@distro_arch=$(subst docker-rpm-,,$@); \
	distro=$$(echo $$distro_arch | sed 's/-x86_64$$//' | sed 's/-aarch64$$//'); \
	arch=$$(echo $$distro_arch | sed 's/^.*-//'); \
	$(MAKE) -f $(PACKAGING_DIR)/Makefile _docker-rpm-internal DISTRO=$$distro ARCH=$$arch

# Internal target for RPM builds
_docker-rpm-internal:
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
	# Copy .rpm files to releases directory \
	if [ -d /tmp/rtpmidid-output-$(DISTRO)-$(ARCH) ]; then \
		cp -v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.rpm $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/ 2>/dev/null || \
		echo "Warning: No .rpm files found in output"; \
		rm -rf /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
	else \
		echo "Error: Output directory not found"; \
	fi; \
	\
	echo "Package built successfully: $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/"

