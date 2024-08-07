# Port for test run 
PORT:=10000
# Number of jobs for make
JOBS:=$(shell nproc)

# To easy change to clang, set CXX. 
# ENABLE_PCH sound like a good idea, but for massive parallelist (my comp has 32 CPU threads), it
# stalls the parallelist waiting to compile the Pre Compiled Headers.
CMAKE_EXTRA_ARGS := -DCMAKE_CXX_COMPILER=${CXX} -DENABLE_PCH=OFF


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
	@echo " statemachines -- Generate the files for the state machines"
	@echo
	@echo "Variables:"
	@echo 
	@echo " PORT=10000"
	@echo 

.PHONY: build
build: build/bin/rtpmidid

build/bin/rtpmidid: src/* tests/* CMakeLists.txt
	mkdir -p build
	cd build &&	cmake .. -DCMAKE_BUILD_TYPE=Release -GNinja $(CMAKE_EXTRA_ARGS)
	cd build && ninja

build-dev: 
	mkdir -p build
	cd build &&	cmake .. -DCMAKE_BUILD_TYPE=Debug $(CMAKE_EXTRA_ARGS)
	cd build && make -j$(JOBS)

build-deb:
	mkdir -p build
	cd build &&	cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=OFF -GNinja $(CMAKE_EXTRA_ARGS) -DLDD=system
	cd build && ninja

build-make:
	mkdir -p build
	cd build &&	cmake .. -DCMAKE_BUILD_TYPE=Release -GMakefile $(CMAKE_EXTRA_ARGS)
	cd build && make -j$(JOBS)
	

man: 
	mkdir -p build/man/
	pandoc rtpmidid.1.md -s -t man -o build/man/rtpmidid.1
	pandoc rtpmidid-cli.1.md -s -t man -o build/man/rtpmidid-cli.1

.PHONY: clean
clean:
	rm -rf build
	rm -rf debian/rtpmidid
	rm -rf debian/librtpmidid0
	rm -rf debian/librtpmidid0-dev

VALGRINDFLAGS := --leak-check=full --error-exitcode=1
RTPMIDID_ARGS := --ini default.ini --port ${PORT} --name devel --control /tmp/rtpmidid.sock

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

.PHONY: test build-test
test: build-test
	mkdir -p build
	cd build &&	cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -G "Unix Makefiles" $(CMAKE_EXTRA_ARGS)
	cd build/tests && make -j
	cd build/tests && CTEST_OUTPUT_ON_FAILURE=1 make test

statemachines:
	scripts/statemachine_to_cpp.py \
		lib/STATEMACHINES.md \
		--name rtpclient_t \
		--header include/rtpmidid/rtpclient_statemachine.hpp \
		--source lib/rtpclient_statemachine.cpp 

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
	cd cli && make compile
	cp build/rtpmidid-cli $(PREFIX)/usr/bin/rtpmidid-cli
	mkdir -p $(PREFIX)/etc/systemd/system/
	cp debian/rtpmidid.service $(PREFIX)/etc/systemd/system/
	mkdir -p $(PREFIX)/etc/rtpmidid/
	cp default.ini $(PREFIX)/etc/rtpmidid/
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


