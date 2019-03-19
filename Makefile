all: compile


compile: build/rtpmidid

build/rtpmidid: src/* CMakeLists.txt conanfile.txt
	mkdir -p build
	cd build &&	conan install ..
	cd build &&	cmake ..
	cd build && make


clean:
	rm -rf target

## OLD Python daemon

.PHONY: setup
setup: requirements.txt
	python3 -m venv env
	env/bin/pip install -r requirements.txt

env/: setup

run:
	env/bin/python3 rtpmidi.py
