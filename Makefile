.PHONY: help
help:
	@echo "Makefile for rtpmidid"
	@echo
	@echo "compile -- Creates the build directory and compiles the rtpmidid"
	@echo "run     -- Compiles and runs the daemon"
	@echo "setup   -- Creates the socket control file"
	@echo "clean   -- Cleans project"
	@echo

.PHONY: compile
compile: build/bin/rtpmidid

build/bin/rtpmidid: src/* tests/* CMakeLists.txt
	mkdir -p build
	cd build &&	cmake ..
	cd build && make -j

.PHONY: clean
clean:
	rm -rf build

VALGRINDFLAGS := --leak-check=full --error-exitcode=1
RTPMIDID_ARGS := --port 10000

.PHONY: run run-valgrind
run: build/src/rtpmidid
	build/src/rtpmidid $(RTPMIDID_ARGS)

run-valgrind: build/src/rtpmidid
	valgrind --leak-check=full --show-leak-kinds=all build/src/rtpmidid $(RTPMIDID_ARGS)

.PHONY: setup
setup:
	sudo mkdir -p /var/run/rtpmidid
	sudo chown $(shell whoami) /var/run/rtpmidid

.PHONY: test test_mdns test_rtppeer test_rtpserver
test: test_mdns test_rtppeer test_rtpserver

test_mdns: compile
	valgrind $(VALGRINDFLAGS) build/tests/test_mdns

test_rtppeer: compile
	valgrind $(VALGRINDFLAGS) build/tests/test_rtppeer

test_rtpserver: compile
	valgrind $(VALGRINDFLAGS) build/tests/test_rtpserver

