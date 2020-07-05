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
test: test_rtppeer test_rtpserver

test_rtppeer: build
	valgrind $(VALGRINDFLAGS) build/tests/test_rtppeer

test_rtpserver: build
	valgrind $(VALGRINDFLAGS) build/tests/test_rtpserver

.PHONY: deb
deb:
	$(eval VERSION := $(shell git describe --match "v[0-9]*" --tags --abbrev=5 HEAD | sed 's/^v//g' | sed 's/-/~/g' ))
	$(eval DATE := $(shell date -R))
	sed -i "1s/.*/rtpmidid (${VERSION}) unstable; urgency=medium/" debian/changelog
	sed -i "5s/.*/ -- David Moreno <dmoreno@coralbits.com>  ${DATE}/" debian/changelog

	dpkg-buildpackage --no-sign

ifeq ($(PREFIX),)
    PREFIX := /usr/local
endif
.PHONY: install

install: install-rtpmidid install-librtpmidid0 install-librtpmidid0-dev

install-rtpmidid: build
	mkdir -p $(PREFIX)/usr/bin/ 
	cp build/src/rtpmidid $(PREFIX)/usr/bin/
	cp cli/rtpmidid-cli.py $(PREFIX)/usr/bin/rtpmidid-cli
	mkdir -p $(PREFIX)/etc/systemd/system/
	cp debian/rtpmidid.service $(PREFIX)/etc/systemd/system/
	mkdir -p $(PREFIX)/usr/share/doc/rtpmidid/
	cp README.md $(PREFIX)/usr/share/doc/rtpmidid/
	cp LICENSE-daemon.txt $(PREFIX)/usr/share/doc/rtpmidid/LICENSE.txt

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


ETH = $(shell route | awk '$$1 == "default" { print $$8 }')
PORT = 5004

PORT1 = $(shell echo | awk '{print ${PORT} + 1}')

.PHONY: dump
dump:
	@echo
	@echo "Set ETH port or PORT with: make dump ETH=eth0 PORT=5004"
	@echo
	rm -f dump.${PORT}
	tcpdump '(port ${PORT}) or (port ${PORT1})' -s 65536 -w dump.${PORT} -i ${ETH} -v


