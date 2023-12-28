# RTP MIDI daemon test cases

There are not enough tests yet. I hope to increase the number in the future, but
here are some notes on what we should test, and some use cases.

## WIP

- [x] Basic mDNS
- [x] Announce check if announced mDNS
- [x] Connect to rtpmidi peers
- [x] Disconnect from rtpmidi peers
- [x] Clock
- [ ] Journal N
- [ ] More journals

## Test cases

## 1. Connect to rtpmidi by Tobias Erichsen

- rtpmidi announces its existence, so rtpmidid ports are created locally
- connect at rtpmidi, and then another rtpmidi port appear.
- Should it be the very same?

## 2. Remote disconnect

- If remote end disconnects, then the alsa connection should be removed.

## 3. does not send CK on connection [fixes]

1. Create two rtpmidid
2. Connect aseqdump to the peer1/peer1
3. Connect vmpk at the peer2/aseqdump
4. [optional] close aseqdump

Should:
Work no more. Send CK and keep connection.

CK is sent by clients when connection finishes, looks like never finishes?

Does:
Stop the connection as nobody does the CK cycles.
Leaves garbage aseqdumps at test2

## 4. Connect directly two rtpmidi ports [fixed]

1. Remote keyboard A
2. Remote synth B
3. Connect local/A to local/B

Should:
When press key A, receive at B

Does:

- Connect A with local/A, CONNECTED (rtpmidi client and alsa peer)
- DO NOT connect local/B with B. WAITING (alsa listener)
- Not sends data.

Fix: When its an internal connection, special care must be taken for connecting end.
