#!/usr/bin/env python3
# A MIDI file of what fakecore sent to its MT32-pi (FAKECORE_MIDILOG): lines
# "<audio frame> <bytes in hex>", the bytes of the messages in the order sent.
# One tick of the file is one audio frame (44.1kHz), so it plays as the core
# would have sent it: fluidsynth -F out.wav <font.sf2> out.mid
#
# usage: midilog2mid.py <log> <out.mid>

import struct
import sys


def varlen(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, 0x80 | (n & 0x7F))
        n >>= 7
    return bytes(out)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: midilog2mid.py <log> <out.mid>")
    stream = []
    for line in open(sys.argv[1]):
        f = line.split()
        if not f or f[0].startswith("#"):
            continue
        frame = int(f[0])
        stream += [(frame, int(b, 16)) for b in f[1:]]

    track = bytearray()
    last = None
    msg, msg_frame = [], 0
    messages = 0

    def put(frame, data):
        nonlocal last, messages
        if last is None:
            last = frame
        track.extend(varlen(frame - last))
        track.extend(data)
        last = frame
        messages += 1

    for frame, b in stream:
        if b >= 0x80 and b != 0xF7:
            msg, msg_frame = [b], frame
        elif msg:
            msg.append(b)
        else:
            continue  # data without a status byte
        s = msg[0]
        if s == 0xF0:
            if b == 0xF7:
                put(msg_frame, bytes([0xF0]) + varlen(len(msg) - 1) + bytes(msg[1:]))
                msg = []
        elif len(msg) == (2 if (s & 0xE0) == 0xC0 else 3):
            put(msg_frame, bytes(msg))
            msg = []
    track.extend(varlen(0) + b"\xff\x2f\x00")

    with open(sys.argv[2], "wb") as out:
        # format 0, one track, 22050 ticks a quarter note at the default
        # 120 beats a minute: a tick is 1/44100 s
        out.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 22050))
        out.write(b"MTrk" + struct.pack(">I", len(track)) + track)
    print("%d messages, %.1f s" % (messages, (stream[-1][0] - stream[0][0]) / 44100 if stream else 0))


main()
