PYTHON=python3

compile: rtpmidid-cli.py
	$(PYTHON) -m zipapp rtpmidid-cli.py -o ../build/rtpmidid-cli -p /usr/bin/python3 

run: compile
	../build/rtpmidid-cli
