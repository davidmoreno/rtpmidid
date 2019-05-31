all: compile

compile: build/bin/rtpmidid

build/bin/rtpmidid: src/* tests/* CMakeLists.txt
	mkdir -p build
	cd build &&	cmake ..
	cd build && make -j

clean:
	rm -rf build

test: compile
	cd build; make -j
	valgrind build/tests/test_mdns
	valgrind build/tests/test_rtppeer
