all: compile

compile: build/bin/rtpmidid

build/bin/rtpmidid: src/* tests/* CMakeLists.txt
	mkdir -p build
	cd build &&	cmake ..
	cd build && make -j

clean:
	rm -rf build

.PHONY: test test_mdns test_rtppeer test_rtpserver

test: test_mdns test_rtppeer test_rtpserver

test_mdns: compile
	valgrind build/tests/test_mdns

test_rtppeer: compile
	valgrind build/tests/test_rtppeer

test_rtpserver: compile
	valgrind --leak-check=full build/tests/test_rtpserver
