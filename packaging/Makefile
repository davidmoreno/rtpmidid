# Docker-based Debian package builder
# Supports multiple distributions and architectures

# Default values
DISTRO ?= debian-trixie
ARCH ?= amd64
RELEASES_DIR ?= $(SOURCE_DIR)/releases

# Available distributions
DISTROS := debian-trixie ubuntu-24.04 ubuntu-25.10

# Available architectures
ARCHES := amd64 arm64 armhf

# Map architecture to Docker platform
DOCKER_PLATFORM_amd64 := linux/amd64
DOCKER_PLATFORM_arm64 := linux/arm64
DOCKER_PLATFORM_armhf := linux/arm/v7

# Get Docker platform for architecture
get_platform = $(DOCKER_PLATFORM_$(ARCH))

# Docker image name
IMAGE_NAME = rtpmidid-builder-$(DISTRO)-$(ARCH)
DOCKERFILE = docker/Dockerfile.$(DISTRO)

# Source directory (parent of packaging/)
SOURCE_DIR = $(shell dirname $(shell pwd))
PACKAGING_DIR = $(shell pwd)

.PHONY: help docker-deb docker-deb-all docker-deb-%
.PHONY: $(foreach distro,$(DISTROS),$(foreach arch,$(ARCHES),docker-deb-$(distro)-$(arch)))

help:
	@echo "Docker Debian Package Builder"
	@echo ""
	@echo "Usage:"
	@echo "  make docker-deb [DISTRO=<distro>] [ARCH=<arch>]"
	@echo "  make docker-deb-all"
	@echo ""
	@echo "Available distributions: $(DISTROS)"
	@echo "Available architectures: $(ARCHES)"
	@echo ""
	@echo "Examples:"
	@echo "  make docker-deb"
	@echo "  make docker-deb DISTRO=ubuntu-24.04 ARCH=arm64"
	@echo "  make docker-deb-all"

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
		/usr/local/bin/rtpmidid-build.sh || \
	( \
		echo "Trying with root user..."; \
		docker run --rm \
			--platform $$platform \
			-v $(SOURCE_DIR):/source:z \
			-v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH):/output:z \
			$(IMAGE_NAME) \
			/usr/local/bin/rtpmidid-build.sh \
	); \
	\
	# Copy .deb files to releases directory \
	if [ -d /tmp/rtpmidid-output-$(DISTRO)-$(ARCH) ]; then \
		cp -v /tmp/rtpmidid-output-$(DISTRO)-$(ARCH)/*.deb $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/ 2>/dev/null || \
		echo "Warning: No .deb files found in output"; \
		rm -rf /tmp/rtpmidid-output-$(DISTRO)-$(ARCH); \
	else \
		echo "Error: Output directory not found"; \
	fi; \
	\
	echo "Package built successfully: $(RELEASES_DIR)/$(DISTRO)/$(ARCH)/"

