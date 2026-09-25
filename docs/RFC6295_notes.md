# Basic RTPMIDI Cheat Sheet

The RFC has data all around the place, but here I have the headers and the bit
meanings.

Implementation spec for Chapter N (the journal that repairs missing NoteOn /
NoteOff): [../architecture/recovery-journal.md](../architecture/recovery-journal.md).

## RTP Header

     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    | V |P|X|  CC   |M|     PT      |        Sequence number        |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                           Timestamp                           |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                             SSRC                              |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                     MIDI command section ...                  |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                       Journal section ...                     |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

| Bit             |  pos  | description                                                                                                              |
| --------------- | :---: | ------------------------------------------------------------------------------------------------------------------------ |
| V               |  0-1  | Version. 2.                                                                                                              |
| P               |   2   | Has paddding. RTP needs it for encryption. 0.                                                                            |
| X               |   3   | Header extension. 0.                                                                                                     |
| CC              |  4-7  | CSRC count. 0.                                                                                                           |
| M               |   8   | Has MIDI data.                                                                                                           |
| PT              | 9-15  | Payload type. Always 0x61. MIDI.                                                                                         |
| Sequence number | 16-31 | Starts random, increase one on each packet. (%2^16). There is an extended one with 32 bits and rollovers.                |
| Timestamp       | 32-63 | Time this packet was generated. On Apple midi the unit is 0.1 ms. (1^-4 seconds). Real RTPMIDI is at session connection. |
| SSRC            | 64-96 | Random unique SSRC for this sender. Same for all the session.                                                            |

Timestamp can be buffered to reduce jitter on the receive end, creating a
continuous lag of a specific length.

## MIDI Command section

     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |B|J|Z|P|LEN... |  MIDI list ...                                |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

| Bit    | pos | description                                                                                                      |
| ------ | :-: | ---------------------------------------------------------------------------------------------------------------- |
| B      |  0  | Length is 12 bits. If true length at 4-7 is MSB, and one more byte.                                              |
| J      |  1  | There is a journal                                                                                               |
| Z      |  2  | First midi command as is in MIDI section. No timestamp for first command.                                        |
| P      |  3  | Phantom MIDI command. The first command is a running command from previous stream.                               |
| length | 4-7 | How many bytes. May be extended with the B bit.                                                                  |
| MIDI   | ... | MIDI data, then timestamp, MIDI data, timestamp and so on.. or timestamp, midi data and so on. Depends on Z bit. |

Timestamps in running lenght encoding. https://en.wikipedia.org/wiki/Run-length_encoding

# Journal

Journal is totally optional, and on a home network maybe even bad idea as if you
use a router or switch (most common scenario nowadays). Check your errors on
ifconfig and on normal LAN is always zero.

## Journal Bits

     0                   1                   2
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |S|Y|A|H|TOTCHAN|   Checkpoint Packet Seqnum    |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

| Bit                      | pos  | description                                                              |
| ------------------------ | :--: | ------------------------------------------------------------------------ |
| S                        |  0   | Single packet loss hint. **1 by default**; 0 means some element in this journal codes a command stored in packet I-1 (then all containing elements and this bit are 0 too). |
| Y                        |  1   | Has system journal                                                       |
| A                        |  2   | Has channel journals. Needs totchan.                                     |
| H                        |  3   | Enhanced Chapter C encoding. 0 in our streams.                           |
| TOTCHAN                  | 4-7  | Nr channels -1 (has totchan + 1 channels)                                |
| Checkpoint packet seqnum | 8-23 | Seq nr of the checkpoint packet C. The journal covers packets C..I-1, where I is the packet carrying it. C == I means empty history. |

Why the S bit exists: in the common case of a **single** lost packet, a receiver
may skip every element whose S bit is 1, because S=1 guarantees the element
codes nothing from the lost packet. If the lost packet's MIDI list was empty,
the top-level S is 1 and no repair is needed at all. For multi-packet losses the
S bits are ignored and everything is parsed.

## Channel Journal

One for each (TOTCHAN + 1)

     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |S| CHAN  |H|      LENGTH       |P|C|M|W|N|E|T|A|  Chapters ... |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

| Bit    |  pos   | description                                                              |
| ------ | :----: | ------------------------------------------------------------------------ |
| S      |   0    | S bit for this channel journal (see top-level S semantics).               |
| CHAN   |  1-4   | Channel number                                                           |
| H      |   5    | Whether controllers are Enhanced Chapter C.                              |
| LENGHT |  6-15  | Length of the channel journal **including these 3 header octets** and all chapters. Receivers MUST use it to skip a journal they do not understand (RFC 6295 A.1). |
| P      | 16 / 0 | Chapter P. Program Change.                                               |
| C      | 17 / 1 | Chapter C. Control Change.                                               |
| M      | 18 / 2 | Chapter M. Parameter System.                                             |
| W      | 19 / 3 | Chapter W. Pitch Wheel.                                                  |
| N      | 20 / 4 | Chapter N. Note On/Off                                                   |
| E      | 21 / 5 | Chapter E. Note Command Extras                                           |
| T      | 22 / 6 | Chapter T. After Touch.                                                  |
| A      | 23 / 7 | Chapter A. Poly Aftertouch.                                              |

The S bit allows a faster implementation for the single-packet-loss case: skip
every element with S=1, because those elements provably code nothing from the
lost packet.

## Chapter P

     0                   1                   2
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |S|   PROGRAM   |B|   BANK-MSB  |X|  BANK-LSB   |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

## Chapter C

     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 8 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |S|     LEN     |S|   NUMBER    |A|  VALUE/ALT  |S|   NUMBER    |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |A|  VALUE/ALT  |  ....                                         |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

## Chapter M

     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 8 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |S|     LEN     |S|   NUMBER    |A|  VALUE/ALT  |S|   NUMBER    |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |A|  VALUE/ALT  |  ....                                         |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

# Chapter W

     0                   1
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |S|     FIRST   |R|    SECOND   |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

## Chapter N

     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 8 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |B|     LEN     |  LOW  | HIGH  |S|   NOTENUM   |Y|  VELOCITY   |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |S|   NOTENUM   |Y|  VELOCITY   |             ....              |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |    OFFBITS    |    OFFBITS    |     ....      |    OFFBITS    |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

| Bit  |    pos    | description                                                                 |
| ---- | :-------: | --------------------------------------------------------------------------- |
| B    |     0     | S-style bit for the OFFBITS field. **1 by default**; MUST be 0 if packet I-1's MIDI section contains a NoteOff for this channel (then all containing S bits MUST be 0 too). |
| LEN  |    1-7    | Number of 2-octet note logs. 0 = empty list. `LEN=127` with `LOW=15, HIGH=0` codes **128** logs; any other `LEN=127` codes 127. |
| LOW  |    8-11   | Index of the first OFFBITS octet.                                           |
| HIGH |   12-15   | Index of the last OFFBITS octet. If `LOW <= HIGH` there are `HIGH-LOW+1` OFFBITS octets. `(LOW=15,HIGH=0)` and `(LOW=15,HIGH=1)` code an **empty** NoteOff bitfield. Other `LOW > HIGH` values MUST NOT be emitted. |
| S    | 16n       | S bit of note log `n`.                                                      |
| Y    | 16n + 8   | Recommendation to play (Y=1) or skip (Y=0) the recovered NoteOn.             |

Chapter N rules that are easy to get wrong:

- Note log list: 2 octets per entry, `NOTENUM` 7-bit, `VELOCITY` 7-bit and
  **never zero** (a zero-velocity NoteOn is a NoteOff and belongs in OFFBITS).
  Logs MUST be in **oldest-first** order.
- OFFBITS: MSB of an octet is the **lowest** note of its group; MSB of the first
  octet is note `8*LOW`, MSB of the last is note `8*HIGH`. Set bit = a NoteOff
  for that note. NoteOff velocity is not coded.
- A note number MUST NOT appear in both the note log list and OFFBITS.
- The note log codes the **most recent N-active NoteOn** for that note number;
  a set OFFBITS bit codes a note whose **most recent N-active command** was a
  NoteOff.
- "N-active" state is invalidated by CC 120, CC 123-127 and Reset State
  commands (`0xFF`, GM/GM2/DLS SysEx resets).

## Chapter T

    0
    0 1 2 3 4 5 6 7
    +-+-+-+-+-+-+-+-+
    |S|   PRESSURE  |
    +-+-+-+-+-+-+-+-+

## Chapter A

     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 8 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |S|    LEN      |S|   NOTENUM   |X|  PRESSURE   |S|   NOTENUM   |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |X|  PRESSURE   |  ....                                         |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

# SysEx

https://datatracker.ietf.org/doc/html/rfc6295 - pg 17

| Formats  | notes                                | encoding  |
| -------- | ------------------------------------ | --------- |
| Basic    |                                      | F0 ... F7 |
| Multiple | Temporal information, or Big packets |           |
|          | First packet                         | F0 ... F0 |
|          | list have to have some data          | F7 ... F0 |
|          | last may be empty                    | F7 .x. F7 |
|          | Cancel must be empty                 | F7 F4     |
