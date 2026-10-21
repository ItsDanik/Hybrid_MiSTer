#!/usr/bin/env python3
# Compares two renderings of a MIDI file (ours from render.c, FluidSynth's
# from `fluidsynth -F`): the level, how loudness goes over time, the
# spectrum by octave, and how alike the waveforms are.
#
# usage: compare.py <ours.wav> <fluidsynth.wav>
import struct, sys
import numpy as np

RATE = 44100

def load(path):
    d = open(path, 'rb').read()
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(d):
        cid, size = d[pos:pos + 4], struct.unpack_from('<I', d, pos + 4)[0]
        if cid == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', d, pos + 8)
        elif cid == b'data':
            data = d[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    if fmt is None or data is None or fmt[0] != 1 or fmt[1] != 2 or fmt[5] != 16:
        sys.exit('%s: not 16-bit stereo PCM' % path)
    return np.frombuffer(data[:len(data) // 4 * 4], dtype='<i2').reshape(-1, 2).astype(np.float64)

def db(x):
    return 20 * np.log10(np.maximum(x, 1e-9))

def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    # where b's start lies against a's, from 20 s of mono from where the
    # sound starts
    m = min(len(a), len(b))
    nz = np.nonzero(np.abs(a[:m]).sum(axis=1) + np.abs(b[:m]).sum(axis=1))[0]
    s0 = max(int(nz[0]) - 8192, 0) if len(nz) else 0
    n = min(m - s0, 20 * RATE)
    ma, mb = a[s0:s0 + n].mean(axis=1), b[s0:s0 + n].mean(axis=1)
    size = 1 << int(np.ceil(np.log2(2 * n)))
    xc = np.fft.irfft(np.fft.rfft(ma, size) * np.conj(np.fft.rfft(mb, size)), size)
    lag = int(np.argmax(np.concatenate((xc[-4096:], xc[:4096])))) - 4096
    if lag > 0:
        a = a[lag:]
    elif lag < 0:
        b = b[-lag:]
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    print('offset: %d frames (%s later)' % (abs(lag), 'ours' if lag > 0 else 'FluidSynth\'s'))

    ra, rb = np.sqrt((a ** 2).mean()), np.sqrt((b ** 2).mean())
    print('level: ours %.1f dBFS, FluidSynth %.1f dBFS, difference %+.2f dB' %
          (db(ra / 32768), db(rb / 32768), db(ra / rb)))
    print('peaks: ours %d, FluidSynth %d' % (np.abs(a).max(), np.abs(b).max()))

    # loudness over time, 50 ms windows above -60 dBFS
    w = RATE // 20
    m = n // w
    wa = db(np.sqrt((a[:m * w] ** 2).reshape(m, -1).mean(axis=1)) / 32768)
    wb = db(np.sqrt((b[:m * w] ** 2).reshape(m, -1).mean(axis=1)) / 32768)
    keep = (wa > -60) | (wb > -60)
    d = wa[keep] - wb[keep]
    print('loudness, 50 ms windows: correlation %.4f, difference mean %+.2f dB, |difference| 95%% under %.2f dB, max %.2f dB' %
          (np.corrcoef(wa[keep], wb[keep])[0, 1], d.mean(), np.percentile(np.abs(d), 95), np.abs(d).max()))

    # spectrum by octave
    seg = 1 << 15
    spa = np.zeros(seg // 2 + 1)
    spb = np.zeros(seg // 2 + 1)
    win = np.hanning(seg)
    for s in range(0, n - seg, seg):
        spa += np.abs(np.fft.rfft(a[s:s + seg].mean(axis=1) * win)) ** 2
        spb += np.abs(np.fft.rfft(b[s:s + seg].mean(axis=1) * win)) ** 2
    f = np.fft.rfftfreq(seg, 1 / RATE)
    bands = []
    lo = 31.25
    while lo < 16000:
        sel = (f >= lo) & (f < 2 * lo)
        bands.append('%d Hz %+.1f' % (lo, 10 * np.log10(spa[sel].sum() / max(spb[sel].sum(), 1e-9))))
        lo *= 2
    print('spectrum, ours against FluidSynth, dB: ' + ', '.join(bands))

    # waveforms: the share of FluidSynth's signal ours does not have
    c = (a * b).sum() / np.sqrt((a ** 2).sum() * (b ** 2).sum())
    res = b - a * (b * a).sum() / max((a * a).sum(), 1e-9)
    print('waveform correlation %.4f, residual %.1f dB under the signal' % (c, db(np.sqrt((res ** 2).mean()) / rb)))

main()
