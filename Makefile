all:

.PHONY: setup
setup: requirements.txt
	python3 -m venv env
	env/bin/pip install -r requirements.txt

env/: setup

run:
	env/bin/python3 rtpmidi.py
