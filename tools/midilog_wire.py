#!/usr/bin/env python3
# What the MT32-pi's cable does to the timing of fakecore's MIDI log
# (FAKECORE_MIDILOG: lines "<audio frame> <bytes in hex>", a message or a
# piece of a SysEx each). The core sends each line's bytes once the audio has
# reached its frame, at 31250 baud, 320us a byte (14.112 audio frames), so a
# burst holds back what comes after it (rtl/hybrid_host.sv, "MT32-pi";
# MH_MidiBacklog()). This sends the log through such a cable and tells how
# late the notes start, and how much shorter they get: a note held back more
# at its start than at its end (the first notes behind a reset or a song's
# settings) is cut short.
#
# usage: midilog_wire.py <log> [late ms, default 5]

import sys

FRAMES_PER_BYTE = 44100 * 320e-6
MS = 1000 / 44100


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit("usage: midilog_wire.py <log> [late ms]")
    late_ms = float(sys.argv[2]) if len(sys.argv) == 3 else 5.0
    free = 0.0
    sounding = {}  # (channel, key) -> (frame wanted, frame sent)
    notes = late = 0
    latest = (0.0, 0)
    shortest = None  # (ms lost, length wanted ms, channel, key, frame)
    for line in open(sys.argv[1]):
        f = line.split()
        if not f or f[0].startswith("#"):
            continue
        frame = int(f[0])
        data = [int(x, 16) for x in f[1:]]
        free = max(free, frame) + len(data) * FRAMES_PER_BYTE
        # the message's last byte has gone out: that is when it is heard
        sent = free
        status = data[0] if data else 0
        kind, ch = status & 0xF0, status & 15
        ends = []
        if kind == 0x90 and len(data) == 3 and data[2] > 0:
            notes += 1
            delay = (sent - frame) * MS
            if delay > late_ms:
                late += 1
            if delay > latest[0]:
                latest = (delay, frame)
            sounding[(ch, data[1])] = (frame, sent)
        elif kind in (0x80, 0x90) and len(data) == 3:
            ends = [(ch, data[1])]
        elif kind == 0xB0 and len(data) == 3 and data[1] in (120, 123):
            ends = [k for k in sounding if k[0] == ch]
        for k in ends:
            if k not in sounding:
                continue
            want_on, sent_on = sounding.pop(k)
            wanted = (frame - want_on) * MS
            lost = wanted - (sent - sent_on) * MS
            if wanted > 0 and (shortest is None or lost > shortest[0]):
                shortest = (lost, wanted, k[0], k[1], want_on)
    print(f"{notes} notes, {late} of them more than {late_ms:g}ms late; "
          f"the latest {latest[0]:.1f}ms (frame {latest[1]})")
    if shortest is not None:
        lost, wanted, ch, key, frame = shortest
        print(f"cut shortest: {lost:.1f}ms of {wanted:.1f}ms "
              f"(channel {ch + 1}, key {key}, frame {frame})")


if __name__ == "__main__":
    main()
