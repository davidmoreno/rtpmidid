help:
	@echo "Makefile for rtpmidid"
	@echo
	@echo "compile -- Creates the build directory and compiles the rtpmidid"
	@echo "run     -- Compiles and runs the daemon"
	@echo "setup   -- Creates the socket control file"
	@echo

compile: build/bin/rtpmidid

build/bin/rtpmidid: src/* tests/* CMakeLists.txt
	mkdir -p build
	cd build &&	cmake ..
	cd build && make -j

clean:
	rm -rf build

.PHONY: test test_mdns test_rtppeer test_rtpserver

test: test_mdns test_rtppeer test_rtpserver

VALGRINDFLAGS := --leak-check=full --error-exitcode=1
RTPMIDID_ARGS := --port 10000

test_mdns: compile
	valgrind $(VALGRINDFLAGS) build/tests/test_mdns

test_rtppeer: compile
	valgrind $(VALGRINDFLAGS) build/tests/test_rtppeer

test_rtpserver: compile
	valgrind $(VALGRINDFLAGS) build/tests/test_rtpserver

run: build/src/rtpmidid
	build/src/rtpmidid $(RTPMIDID_ARGS)

valgrind_run: build/src/rtpmidid
	valgrind build/src/rtpmidid $(RTPMIDID_ARGS)

setup:
	sudo mkdir -p /var/run/rtpmidid
	sudo chown $(shell whoami) /var/run/rtpmidid
