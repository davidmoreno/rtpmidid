# RTP MIDI daemon test cases

There are not enough tests yet. I hope to increase the number in the future, but
here are some ntoes on what we should test, and some use cases.

## WIP

* [x] Basic mDNS
* [ ] Announce check if announced mDNS
* [ ] Connect to rtpmidi peers
* [ ] Disconnect from rtpmidi peers
* [ ] Clock
* [ ] Journal N
* [ ] More journals

## Test cases

1. Connect to rtpmidi by Tobias Erichsen

* rtpmidi announces its existence, so rtpmidid ports are created locally
* connect at rtpmidi, and then another rtpmidi port appear.
* Should it be the very same?


2. Remote disconnect

* If remote end disconnects, then the alsa connection should be removed.
