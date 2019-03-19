all: compile


compile: target/rtpmidid

target/rtpmidid: src/*.cpp
	mkdir -p target
	g++ -o target/rtpmidid src/*.cpp -O3 -std=c++17

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
