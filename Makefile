.PHONY: help
help:
	@echo "Makefile for rtpmidid"
	@echo
	@echo "build   -- Creates the build directory and builds the rtpmidid"
	@echo "run     -- builds and runs the daemon"
	@echo "setup   -- Creates the socket control file"
	@echo "clean   -- Cleans project"
	@echo "deb     -- Generate deb package"
	@echo

.PHONY: build
build: build/bin/rtpmidid

build/bin/rtpmidid: src/* tests/* CMakeLists.txt
	mkdir -p build
	cd build &&	cmake .. -DCMAKE_BUILD_TYPE=Release
	cd build && make -j

build-dev: 
	mkdir -p build
	cd build &&	cmake .. -DCMAKE_BUILD_TYPE=Debug
	cd build && make -j


.PHONY: clean
clean:
	rm -rf build

VALGRINDFLAGS := --leak-check=full --error-exitcode=1
RTPMIDID_ARGS := --port 10000

.PHONY: run run-valgrind gdb
run: build
	build/src/rtpmidid $(RTPMIDID_ARGS)

gdb: build-dev
	gdb build/src/rtpmidid -ex=r --args build/src/rtpmidid  $(RTPMIDID_ARGS)

run-valgrind: build
	valgrind --leak-check=full --show-leak-kinds=all build/src/rtpmidid $(RTPMIDID_ARGS)

.PHONY: setup
setup:
	sudo mkdir -p /var/run/rtpmidid
	sudo chown $(shell whoami) /var/run/rtpmidid

.PHONY: test test_mdns test_rtppeer test_rtpserver
test: test_mdns test_rtppeer test_rtpserver

test_mdns: build
	valgrind $(VALGRINDFLAGS) build/tests/test_mdns

test_rtppeer: build
	valgrind $(VALGRINDFLAGS) build/tests/test_rtppeer

test_rtpserver: build
	valgrind $(VALGRINDFLAGS) build/tests/test_rtpserver

.PHONY: deb
deb:
	dpkg-buildpackage --no-sign

ifeq ($(PREFIX),)
    PREFIX := /usr/local
endif
.PHONY: install
install: build
	mkdir -p $(PREFIX)/usr/bin/
	cp build/src/rtpmidid $(PREFIX)/usr/bin/
	cp cli/rtpmidid-cli.py $(PREFIX)/usr/bin/rtpmidid-cli
	mkdir -p $(PREFIX)/etc/systemd/system/
	cp debian/rtpmidid.service $(PREFIX)/etc/systemd/system/
	mkdir -p $(PREFIX)/usr/share/doc/rtpmidid/
	cp README.md $(PREFIX)/usr/share/doc/rtpmidid/
	cp LICENSE $(PREFIX)/usr/share/doc/rtpmidid/


