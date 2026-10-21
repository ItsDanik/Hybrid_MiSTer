// Standard MIDI files: see smf.h.
#include "smf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int seq;
    smf_event e;
} raw_event;

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int varlen(const uint8_t** p, const uint8_t* end, uint32_t* v) {
    int n;

    *v = 0;
    for (n = 0; n < 4 && *p < end; n++) {
        uint8_t b = *(*p)++;

        *v = *v << 7 | (b & 0x7f);
        if (!(b & 0x80)) {
            return 1;
        }
    }
    return 0;
}

static int cmp_raw(const void* a, const void* b) {
    const raw_event *x = a, *y = b;

    if (x->e.tick != y->e.tick) {
        return x->e.tick < y->e.tick ? -1 : 1;
    }
    if (x->e.track != y->e.track) {
        return x->e.track - y->e.track;
    }
    return x->seq - y->seq;
}

int smf_load(const char* path, smf_song* song, char* err, int errlen) {
    FILE* f = fopen(path, "rb");
    long size;
    uint8_t* d;
    const uint8_t *p, *end;
    int ntracks, division, t, n = 0, cap = 4096, k;
    raw_event* raw;
    double seconds = 0;
    uint32_t last_tick = 0;
    int tempo = 500000;

    memset(song, 0, sizeof(*song));
    if (f == NULL) {
        snprintf(err, (size_t)errlen, "cannot be read");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = malloc((size_t)size + 1);
    if (d == NULL || fread(d, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(d);
        snprintf(err, (size_t)errlen, "cannot be read");
        return 0;
    }
    fclose(f);
    // a RIFF MIDI file has the standard file in its "data" chunk
    p = d;
    end = d + size;
    if (size > 20 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "RMID", 4) == 0) {
        p = d + 12;
        while (p + 8 <= end && memcmp(p, "data", 4) != 0) {
            p += 8 + (p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24);
        }
        p += 8;
    }
    if (p + 14 > end || memcmp(p, "MThd", 4) != 0) {
        free(d);
        snprintf(err, (size_t)errlen, "is no MIDI file");
        return 0;
    }
    ntracks = p[10] << 8 | p[11];
    division = p[12] << 8 | p[13];
    if (division & 0x8000) {
        free(d);
        snprintf(err, (size_t)errlen, "has SMPTE time, which is not supported");
        return 0;
    }
    p += 8 + be32(p + 4);
    raw = malloc((size_t)cap * sizeof(*raw));
    for (t = 0; t < ntracks && p + 8 <= end; t++) {
        const uint8_t* q;
        const uint8_t* tend;
        uint32_t tick = 0;
        uint8_t running = 0;

        if (memcmp(p, "MTrk", 4) != 0) {
            p += 8 + be32(p + 4);
            t--;
            continue;
        }
        q = p + 8;
        tend = q + be32(p + 4);
        if (tend > end) {
            tend = end;
        }
        p = tend;
        while (q < tend) {
            uint32_t delta, len;
            uint8_t st;
            raw_event r;

            if (!varlen(&q, tend, &delta) || q >= tend) {
                break;
            }
            tick += delta;
            memset(&r, 0, sizeof(r));
            r.e.tick = tick;
            r.e.track = t;
            st = *q;
            if (st & 0x80) {
                q++;
            } else {
                st = running;
            }
            if (st == 0xff) {
                uint8_t type;

                if (q >= tend) {
                    break;
                }
                type = *q++;
                if (!varlen(&q, tend, &len) || q + len > tend) {
                    break;
                }
                if (type == 0x2f) {
                    song->end_tick = tick > song->end_tick ? tick : song->end_tick;
                    break;
                }
                if (type == 0x51 && len == 3) {
                    r.e.status = 0xff;
                    r.e.tempo = q[0] << 16 | q[1] << 8 | q[2];
                    if (r.e.tempo == 0) {
                        r.e.tempo = 1;
                    }
                } else {
                    q += len;
                    continue;
                }
                q += len;
            } else if (st == 0xf0 || st == 0xf7) {
                if (!varlen(&q, tend, &len) || q + len > tend) {
                    break;
                }
                r.e.status = 0xf0;
                r.e.data = q;
                r.e.length = (int)len;
                if (len > 0 && q[len - 1] == 0xf7) {
                    r.e.length--;
                }
                q += len;
                running = 0;
            } else if (st >= 0x80 && st < 0xf0) {
                int nd = (st & 0xe0) == 0xc0 ? 1 : 2;

                if (q + nd > tend) {
                    break;
                }
                running = st;
                r.e.status = st;
                r.e.data1 = q[0] & 0x7f;
                r.e.data2 = nd == 2 ? q[1] & 0x7f : 0;
                q += nd;
            } else {
                break; // a system message a file cannot have
            }
            song->end_tick = tick > song->end_tick ? tick : song->end_tick;
            if (n == cap) {
                cap *= 2;
                raw = realloc(raw, (size_t)cap * sizeof(*raw));
            }
            r.seq = n;
            raw[n++] = r;
        }
    }
    qsort(raw, (size_t)n, sizeof(*raw), cmp_raw);
    song->events = malloc((size_t)(n ? n : 1) * sizeof(smf_event));
    for (k = 0; k < n; k++) {
        seconds += (double)(raw[k].e.tick - last_tick) * tempo / 1e6 / division;
        last_tick = raw[k].e.tick;
        if (raw[k].e.status == 0xff) {
            tempo = raw[k].e.tempo;
        }
        raw[k].e.seconds = seconds;
        song->events[song->count++] = raw[k].e;
    }
    song->length = seconds;
    song->division = division;
    song->tracks = t;
    song->file = d;
    free(raw);
    return 1;
}

void smf_free(smf_song* song) {
    free(song->events);
    free(song->file);
    memset(song, 0, sizeof(*song));
}
