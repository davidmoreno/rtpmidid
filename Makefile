all: compile


compile: build/bin/rtpmidid

build/bin/rtpmidid: src/* tests/* CMakeLists.txt conanfile.txt
	mkdir -p build
	cd build &&	conan install ..
	cd build &&	cmake ..
	cd build && make -j


clean:
	rm -rf build

runcpp: build/bin/rtpmidid
	timeout 10 valgrind build/bin/rtpmidid

test: compile
	cd build; make -j test


## OLD Python daemon

.PHONY: setup
setup: requirements.txt
	python3 -m venv env
	env/bin/pip install -r requirements.txt

env/: setup

run:
	env/bin/python3 rtpmidi.py
