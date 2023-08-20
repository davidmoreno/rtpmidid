PORT := 10000
CXX:= /usr/bin/g++-12

.PHONY: help
help:
	@echo "Makefile for rtpmidid"
	@echo
	@echo " build     -- Creates the build directory and builds the rtpmidid"
	@echo " build-dev -- Creates the build directory and builds the rtpmidid for debugging"
	@echo " run       -- builds and runs the daemon"
	@echo " setup     -- Creates the socket control file"
	@echo " clean     -- Cleans project"
	@echo " deb       -- Generate deb package"
	@echo " test      -- Runs all test"
	@echo " install   -- Installs to PREFIX or DESTDIR (default /usr/local/)"
	@echo " man       -- Generate man pages"
	@echo
	@echo " gdb      -- Run inside gdb, to capture backtrace of failures (bt). Useful for bug reports."
	@echo " capture  -- Capture packets with tcpdump. Add this to bug reports."
	@echo
	@echo "Variables:"
	@echo 
	@echo " PORT=10000"
	@echo 

.PHONY: build
build: build/bin/rtpmidid

build/bin/rtpmidid: src/* tests/* CMakeLists.txt
	mkdir -p build
	cd build &&	cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=${CXX}
	cd build && make -j

build-dev: 
	mkdir -p build
	cd build &&	cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=${CXX}
	cd build && make -j


man: 
	mkdir -p build/man/
	pandoc rtpmidid.1.md -s -t man -o build/man/rtpmidid.1
	pandoc rtpmidid-cli.1.md -s -t man -o build/man/rtpmidid-cli.1

.PHONY: clean
clean:
	rm -rf build

VALGRINDFLAGS := --leak-check=full --error-exitcode=1
RTPMIDID_ARGS := --port ${PORT} --name devel

.PHONY: run run-valgrind gdb
run: build-dev
	build/src/rtpmidid $(RTPMIDID_ARGS)

gdb: build-dev
	gdb build/src/rtpmidid -ex=r --args build/src/rtpmidid  $(RTPMIDID_ARGS)

run-valgrind: build
	valgrind --leak-check=full --show-leak-kinds=all build/src/rtpmidid $(RTPMIDID_ARGS)

PORT1 = $(shell echo | awk '{print ${PORT} + 1}')

.PHONY: dump
capture:
	rm -f /tmp/rtpmidid-${PORT}.dump
	@echo "\033[1;32m"
	@echo
	@echo "   Captured data will be at /tmp/rtpmidid-${PORT}.dump"
	@echo
	@echo "   Press Control-C to stop capturing.  "
	@echo "\033[0m"
	@echo
	tcpdump -i any '(udp port ${PORT}) or (udp port ${PORT1})' -v -w /tmp/rtpmidid-${PORT}.dump

.PHONY: setup
setup:
	sudo mkdir -p /var/run/rtpmidid
	sudo chown $(shell whoami) /var/run/rtpmidid

.PHONY: test test_mdns test_rtppeer test_rtpserver
test: test_rtppeer test_rtpserver

test_rtppeer: build
	valgrind $(VALGRINDFLAGS) build/tests/test_rtppeer

test_rtpserver: build
	valgrind $(VALGRINDFLAGS) build/tests/test_rtpserver

.PHONY: deb
deb:
	debian/update-changelog.py

	dpkg-buildpackage --no-sign

ifneq ($(DESTDIR),)
    PREFIX := $(DESTDIR)
endif
ifeq ($(PREFIX),)
    PREFIX := /usr/local/
endif
.PHONY: install

install: install-rtpmidid install-librtpmidid0 install-librtpmidid0-dev

install-rtpmidid: build man
	mkdir -p $(PREFIX)/usr/bin/ 
	cp build/src/rtpmidid $(PREFIX)/usr/bin/
	cp cli/rtpmidid-cli.py $(PREFIX)/usr/bin/rtpmidid-cli
	mkdir -p $(PREFIX)/etc/systemd/system/
	cp debian/rtpmidid.service $(PREFIX)/etc/systemd/system/
	mkdir -p $(PREFIX)/usr/share/doc/rtpmidid/
	cp README.md $(PREFIX)/usr/share/doc/rtpmidid/
	cp LICENSE-daemon.txt $(PREFIX)/usr/share/doc/rtpmidid/LICENSE.txt
	mkdir -p $(PREFIX)/usr/share/man/man1/
	cp build/man/rtpmidid.1 $(PREFIX)/usr/share/man/man1/
	cp build/man/rtpmidid-cli.1 $(PREFIX)/usr/share/man/man1/

install-librtpmidid0: build
	mkdir -p $(PREFIX)/usr/lib/ 
	cp -a build/lib/lib*so* $(PREFIX)/usr/lib/
	mkdir -p $(PREFIX)/usr/share/doc/librtpmidid0/
	cp README.md $(PREFIX)/usr/share/doc/librtpmidid0/
	cp README.librtpmidid.md $(PREFIX)/usr/share/doc/librtpmidid0/
	cp LICENSE-lib.txt $(PREFIX)/usr/share/doc/librtpmidid0/LICENSE.txt

install-librtpmidid0-dev: build
	mkdir -p $(PREFIX)/usr/lib/ $(PREFIX)/usr/include/
	cp -a build/lib/lib*.a $(PREFIX)/usr/lib/
	cp -a include/rtpmidid $(PREFIX)/usr/include/
	mkdir -p $(PREFIX)/usr/share/doc/librtpmidid0-dev/
	cp README.md $(PREFIX)/usr/share/doc/librtpmidid0-dev/
	cp README.librtpmidid.md $(PREFIX)/usr/share/doc/librtpmidid0-dev/
	cp LICENSE-lib.txt $(PREFIX)/usr/share/doc/librtpmidid0-dev/LICENSE.txt


