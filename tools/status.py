#!/usr/bin/env python3
"""Print what the hybrid core publishes (run on the MiSTer with the core loaded).
usage: status.py [seconds]   samples the status block 20 times per second"""
import mmap, os, struct, sys, time

fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
m = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x30000000)
last = None
MODES = [(320, 200), (640, 200), (640, 400), (320, 240), (640, 480), (800, 600), (1024, 768), (640, 240)] + [(0, 0)] * 8
end = time.monotonic() + (float(sys.argv[1]) if len(sys.argv) > 1 else 0)
while True:
    ctrl = struct.unpack_from("<4I", m, 0)
    st = struct.unpack_from("<32I", m, 0x40)
    line = "joy1 %08x joy2 %08x analog1 %08x osd %08x%08x keys %s" % (
        st[4], st[5], st[6], st[9], st[8], "".join("%08x" % k for k in st[16:32]).lstrip("0") or "0")
    # host version 6: video mode and how it is shown, the video clock asked for and measured
    mode = "%dx%d%s%s" % (MODES[(st[2] >> 12) & 15] + ("i" if st[2] & 0x200 else "", " off" if not st[2] & 0x20000 else ""))
    line += " mode %s vga31 %d clock %.1f/%.3f MHz" % (mode, (st[2] >> 16) & 1, (st[14] & 0xff) / 2, (st[14] >> 16) / 80)
    if line != last:
        print("field %d ctrl %08x %08x: %s" % (st[1], ctrl[0], ctrl[1], line), flush=True)
        last = line
    if time.monotonic() >= end:
        break
    time.sleep(0.05)
