// danik_hybrid_ports_ui - the screens of danik_hybrid_ports.sh, drawn on the
// framebuffer: which hybrid ports to have, and Downloader's work on them.
//
// Runs ON THE MISTER, started by danik_hybrid_ports.sh from the Scripts menu.
// It is the screen, the pad, the keyboard and the mouse, nothing else: what
// is decided and written stays in the script. Over SSH, with no framebuffer,
// the script does without it.
//
//   danik_hybrid_ports_ui main --table FILE --out FILE
//   danik_hybrid_ports_ui run [--title T] -- COMMAND [ARGS...]
//   danik_hybrid_ports_ui probe | release
//   all: [--keep] [--fb DEVICE] [--geometry WxHxBPP] [--no-mouse] [--debug]
//
// main: the ports and their games with tickboxes, and two settings. The
// table, a line a record, tab separated:
//   S <key> <0|1> <label>                 a setting (the first two lines)
//   G <engine> <game> <0|1> <label>       a game of a port (shown under it,
//                                         also when it is the only one)
// Out: the same lines, with what is ticked when the player chose. Exit 0:
// apply; 10: leave; 12: nothing to draw on; 13: the keys ran out (tests feed
// them on a pipe).
//
// run: COMMAND's output as it comes, the last lines in a box, with a busy
// bar while it works, then "A: Close". Exits with COMMAND's own code.
//
// --keep leaves the console in graphics mode and the picture up on the way
// out, for the next screen to replace without the console's text flashing up
// between them; whoever started keeping ends with release.
//
// The canvas is 320x200, the fewest lines a MiSTer's framebuffer can have,
// with what is drawn kept 12 pixels from the sides and 6 from the top and
// bottom for a CRT's overscan. A larger framebuffer gets it scaled by a whole
// number, centred on the same purple. The colours are those of the hybrid
// core's own picture (hybrid_host.sv), the font is its badge's (Cozette).
//
// Input. The pad arrives as keys, which is how Main_MiSTer passes it to a
// script: the d-pad as arrows, A as Enter, B as Escape, X as Space, Y as
// Tab, L and R as Page Up and Page Down (Start and Select never arrive). A
// keyboard gives the same. A mouse is read from Linux (/dev/input/event*),
// which Main_MiSTer leaves to scripts while their screen is up: a pointer,
// clicks on the tickboxes, the scroll bar and the buttons, the wheel.
//
// Modelled on tty2oled+'s settings screens (tools/settings-ui/config.c of
// Mister-tty2oled-plus). This program is free software: you can redistribute
// it and/or modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, version 3 of the License.

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "cozette.h"

// ---------------------------------------------------------------------------
// The canvas: 320x200, a colour index a pixel.
// ---------------------------------------------------------------------------
#define CW 320
#define CH 200
#define ROW 14                    // a line of text, 13 of the font and 1 between

enum { C_BG, C_WHITE, C_PINK, C_BAR, C_DIM, C_DARK, C_BLACK, C_HOVER, C_TEXT2, NCOLOURS };
static const unsigned int colours[NCOLOURS] = {
  0x7F30A0,   // the background of the core's picture
  0xEFEFEF,   // its text
  0xFF5CB4,   // a lit dot
  0xDF50B0,   // the bar: 3/4 of the way from the background to the pink
  0xC2A3CF,   // dimmed text: 6/10 of the way to the white
  0x4C1D60,   // darker: 4/10 of the way to black
  0x000000,
  0x985AB4,   // a button under the pointer: 2/10 of the way to the white
  0xD9CDDE,   // the run screen's text: 85/100 of the way to the white
};

static unsigned char cv[CH][CW];

static void px(int x, int y, int c) { if (x >= 0 && x < CW && y >= 0 && y < CH) cv[y][x] = (unsigned char)c; }
static void fill(int x, int y, int w, int h, int c) {
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) px(i, j, c);
}
static void frame(int x, int y, int w, int h, int c) {
  fill(x, y, w, 1, c); fill(x, y + h - 1, w, 1, c); fill(x, y, 1, h, c); fill(x + w - 1, y, 1, h, c);
}
// rounded: corners of radius r, filled or outlined
static int corner_inset(int r, int j) {
  // how far in the edge is on row j of a corner (0 the outermost row)
  static const unsigned char in2[] = { 1, 0 }, in3[] = { 2, 1, 0 }, in4[] = { 2, 1, 0, 0 };
  if (r <= 1) return j == 0 && r == 1 ? 1 : 0;
  if (r == 2) return in2[j];
  if (r == 3) return in3[j];
  return j < 4 ? in4[j] : 0;
}
static void rbox(int x, int y, int w, int h, int r, int c, int outline) {
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  for (int j = 0; j < h; j++) {
    int inset = j < r ? corner_inset(r, j) : j >= h - r ? corner_inset(r, h - 1 - j) : 0;
    if (!outline) { fill(x + inset, y + j, w - 2 * inset, 1, c); continue; }
    if (j == 0 || j == h - 1) { fill(x + inset, y + j, w - 2 * inset, 1, c); continue; }
    // the side, and the step to the next row's inset where the corner turns
    int next = j + 1 < r ? corner_inset(r, j + 1) : j + 1 >= h - r ? corner_inset(r, h - 2 - j) : 0;
    int prev = j - 1 < r ? corner_inset(r, j - 1) : j - 1 >= h - r ? corner_inset(r, h - j) : 0;
    int a = inset, b = (prev > inset ? prev : inset);
    (void)next;
    for (int k = a; k <= b; k++) { px(x + k, y + j, c); px(x + w - 1 - k, y + j, c); }
  }
}

static const Glyph *glyph(unsigned code) {
  for (size_t i = 0; i < sizeof cozette / sizeof cozette[0]; i++) if (cozette[i].code == code) return &cozette[i];
  return glyph('?');
}
// The next character of UTF-8 text
static unsigned utf8(const char **s) {
  const unsigned char *p = (const unsigned char *)*s;
  unsigned c = *p++;
  if (c >= 0xE0 && p[0] && p[1]) { c = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F); p += 2; }
  else if (c >= 0xC0 && p[0]) { c = (c & 0x1F) << 6 | (p[0] & 0x3F); p += 1; }
  *s = (const char *)p;
  return c;
}
// y is the top of the 13-pixel cell. Returns the x after the text.
static int text(int x, int y, const char *s, int c) {
  while (*s) {
    const Glyph *g = glyph(utf8(&s));
    int top = y + 10 - (g->y + g->h);
    for (int r = 0; r < g->h; r++) {
      unsigned bits = (unsigned)g->rows[r][0] << 8 | g->rows[r][1];
      for (int b = 0; b < g->w; b++) if (bits & (0x8000u >> b)) px(x + g->x + b, top + r, c);
    }
    x += g->dw;
  }
  return x;
}
static int text_w(const char *s) { int w = 0; while (*s) w += glyph(utf8(&s))->dw; return w; }
// as much of s as fits in w pixels
static void text_cut(int x, int y, const char *s, int w, int c) {
  char buf[256]; size_t n = 0;
  const char *p = s;
  while (*p && n < sizeof buf - 4) {
    const char *q = p; utf8(&q);
    char tmp[256]; memcpy(tmp, buf, n); memcpy(tmp + n, p, (size_t)(q - p)); tmp[n + (size_t)(q - p)] = 0;
    if (text_w(tmp) > w) break;
    memcpy(buf + n, p, (size_t)(q - p)); n += (size_t)(q - p); p = q;
  }
  buf[n] = 0;
  text(x, y, buf, c);
}

static const char *const POINTER[] = {
  "X.........", "XX........", "XWX.......", "XWWX......", "XWWWX.....", "XWWWWX....", "XWWWWWX...",
  "XWWWWWWX..", "XWWWWWWWX.", "XWWWWXXXXX", "XWWXWWX...", "XWX.XWWX..", "XX..XWWX..", "X....XWWX.",
  ".....XWWX.", "......XX.." };
static void pointer(int x, int y) {
  for (int r = 0; r < 16; r++)
    for (int c = 0; POINTER[r][c]; c++)
      if (POINTER[r][c] != '.') px(x + c, y + r, POINTER[r][c] == 'X' ? C_BLACK : C_WHITE);
}

// ---------------------------------------------------------------------------
// The framebuffer.
// ---------------------------------------------------------------------------
static struct {
  int fd, w, h, bpp, stride, sx, sy, ox, oy;
  int ro, go, bo, rl, gl, bl;
  unsigned char *mem;
  size_t len;
  unsigned int pal[NCOLOURS];
} fb = { .fd = -1 };

// The canvas on a screen of w x h: the largest whole number that fits, both
// ways, centred. A mode far wider than a screen (more than five columns to
// two lines: 640x200 and the like, shown 4:3) gets pixels as wide as they
// come out on the screen, to the nearest whole number.
static int fb_fit(int w, int h) {
  if (w < CW || h < CH) return 1;
  fb.sy = h / CH;
  fb.sx = fb.sy;
  if (w * 2 > h * 5) fb.sx = (2 * 3 * w * fb.sy + 4 * h) / (2 * 4 * h);
  if (fb.sx > w / CW) fb.sx = w / CW;
  if (fb.sx < 1) fb.sx = 1;
  if (w * 2 <= h * 5 && fb.sy > fb.sx) fb.sy = fb.sx;
  fb.ox = (w - CW * fb.sx) / 2;
  fb.oy = (h - CH * fb.sy) / 2;
  return 0;
}

static unsigned int fb_colour(unsigned int rgb) {
  unsigned int r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;
  unsigned int v = (r >> (8 - fb.rl)) << fb.ro | (g >> (8 - fb.gl)) << fb.go | (b >> (8 - fb.bl)) << fb.bo;
  if (fb.bpp == 32) v |= 0xFFu << 24 & ~(0xFFu << fb.ro | 0xFFu << fb.go | 0xFFu << fb.bo);
  return v;
}

static int fb_open(const char *dev, const char *geometry) {
  struct fb_var_screeninfo v;
  struct fb_fix_screeninfo fx;
  fb.fd = open(dev, O_RDWR | (geometry ? O_CREAT : 0), 0644);
  if (fb.fd < 0) return -1;
  if (!geometry && ioctl(fb.fd, FBIOGET_VSCREENINFO, &v) == 0 && ioctl(fb.fd, FBIOGET_FSCREENINFO, &fx) == 0) {
    fb.w = (int)v.xres; fb.h = (int)v.yres; fb.bpp = (int)v.bits_per_pixel;
    fb.stride = (int)fx.line_length;
    fb.ro = (int)v.red.offset; fb.go = (int)v.green.offset; fb.bo = (int)v.blue.offset;
    fb.rl = (int)v.red.length; fb.gl = (int)v.green.length; fb.bl = (int)v.blue.length;
    fb.len = (size_t)fb.stride * (size_t)v.yres_virtual;
    if (fb.len < (size_t)fb.stride * (size_t)fb.h) fb.len = (size_t)fb.stride * (size_t)fb.h;
  } else if (geometry && sscanf(geometry, "%dx%dx%d", &fb.w, &fb.h, &fb.bpp) == 3) {
    // a file standing in for the device: tests, and looking at it on a desk
    fb.stride = fb.w * fb.bpp / 8;
    if (fb.bpp == 16) { fb.ro = 11; fb.go = 5; fb.bo = 0; fb.rl = 5; fb.gl = 6; fb.bl = 5; }
    else              { fb.ro = 16; fb.go = 8; fb.bo = 0; fb.rl = fb.gl = fb.bl = 8; }
    fb.len = (size_t)fb.stride * (size_t)fb.h;
    if (ftruncate(fb.fd, (off_t)fb.len) != 0) return -1;
  } else {
    return -1;
  }
  if ((fb.bpp != 16 && fb.bpp != 32) || fb_fit(fb.w, fb.h)) return -1;
  fb.mem = mmap(NULL, fb.len, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fd, 0);
  if (fb.mem == MAP_FAILED) { fb.mem = NULL; return -1; }
  for (int i = 0; i < NCOLOURS; i++) fb.pal[i] = fb_colour(colours[i]);
  return 0;
}

// The canvas onto the screen; the first time, the purple around it as well.
static void fb_present(void) {
  static int margins = 0;
  if (!fb.mem) return;
  int bytes = fb.bpp / 8;
  if (!margins) {
    for (int y = 0; y < fb.h; y++) {
      unsigned char *line = fb.mem + (size_t)y * (size_t)fb.stride;
      for (int x = 0; x < fb.w; x++) {
        if (y >= fb.oy && y < fb.oy + CH * fb.sy && x >= fb.ox && x < fb.ox + CW * fb.sx) continue;
        if (bytes == 4) ((unsigned int *)(void *)line)[x] = fb.pal[C_BG];
        else ((unsigned short *)(void *)line)[x] = (unsigned short)fb.pal[C_BG];
      }
    }
    margins = 1;
  }
  for (int y = 0; y < CH; y++) {
    unsigned char *line = fb.mem + (size_t)(fb.oy + y * fb.sy) * (size_t)fb.stride + (size_t)fb.ox * (size_t)bytes;
    if (bytes == 4) {
      unsigned int *p = (unsigned int *)(void *)line;
      for (int x = 0; x < CW; x++) for (int s = 0; s < fb.sx; s++) *p++ = fb.pal[cv[y][x]];
    } else {
      unsigned short *p = (unsigned short *)(void *)line;
      for (int x = 0; x < CW; x++) for (int s = 0; s < fb.sx; s++) *p++ = (unsigned short)fb.pal[cv[y][x]];
    }
    for (int s = 1; s < fb.sy; s++)
      memcpy(line + (size_t)s * (size_t)fb.stride, line, (size_t)CW * (size_t)fb.sx * (size_t)bytes);
  }
}

// ---------------------------------------------------------------------------
// The terminal: keys in, and the console kept from drawing over us.
// ---------------------------------------------------------------------------
static struct termios tty_saved;
static int tty_raw = 0, tty_graphics = 0, in_tty = 0, keep = 0;

static void tty_restore(void) {
  if (tty_raw) { tcsetattr(0, TCSANOW, &tty_saved); tty_raw = 0; }
  if (keep) return;
  if (tty_graphics) { ioctl(0, KDSETMODE, KD_TEXT); tty_graphics = 0; }
  if (in_tty) { const char *s = "\033[2J\033[H\033[?25h"; if (write(1, s, strlen(s)) < 0) { } }
}
static void on_signal(int sig) { keep = 0; tty_graphics = 1; tty_restore(); _exit(128 + sig); }

static void tty_setup(void) {
  struct termios t;
  in_tty = isatty(0);
  if (!in_tty) return;
  if (tcgetattr(0, &tty_saved) == 0) {
    t = tty_saved;
    cfmakeraw(&t);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &t) == 0) tty_raw = 1;
  }
  // only a virtual console has a mode to set; over SSH this fails, harmlessly
  if (ioctl(0, KDSETMODE, KD_GRAPHICS) == 0) tty_graphics = 1;
  signal(SIGINT, on_signal); signal(SIGTERM, on_signal); signal(SIGHUP, on_signal);
}

enum { K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_OK, K_CANCEL, K_APPLY, K_TAB, K_PGUP, K_PGDN,
       K_HOME, K_END, K_EOF };

static unsigned char inbuf[64];
static int inlen = 0, in_eof = 0;

static int in_fill(int wait_ms) {
  struct pollfd p = { 0, POLLIN, 0 };
  if (in_eof || inlen >= (int)sizeof inbuf) return 0;
  if (poll(&p, 1, wait_ms) <= 0) return 0;
  // off a pipe a byte at a time: what follows is the next screen's
  ssize_t n = read(0, inbuf + inlen, in_tty ? sizeof inbuf - (size_t)inlen : 1);
  if (n <= 0) { if (n == 0 || errno != EINTR) in_eof = 1; return 0; }
  inlen += (int)n;
  return 1;
}
static void in_drop(int n) { memmove(inbuf, inbuf + n, (size_t)(inlen - n)); inlen -= n; }

// One key, if there is one. An Escape on its own is Cancel; one that starts
// an arrow's sequence has the rest right behind it.
static int key_read(void) {
  if (inlen == 0 && !in_fill(0)) return in_eof ? K_EOF : K_NONE;
  int c = inbuf[0];
  if (c == 0x1b) {
    if (inlen == 1) in_fill(in_tty ? 40 : 0);
    if (inlen >= 2 && (inbuf[1] == '[' || inbuf[1] == 'O')) {
      int i = 2;
      while (i >= inlen || (inbuf[i] >= '0' && inbuf[i] <= '9') || inbuf[i] == ';') {
        if (i >= inlen) { if (!in_fill(in_tty ? 40 : 0)) break; continue; }
        i++;
      }
      if (i >= inlen) { in_drop(inlen); return K_NONE; }
      int fin = inbuf[i], num = inbuf[2] >= '0' && inbuf[2] <= '9' ? atoi((char *)inbuf + 2) : 0;
      in_drop(i + 1);
      switch (fin) {
        case 'A': return K_UP;    case 'B': return K_DOWN;
        case 'C': return K_RIGHT; case 'D': return K_LEFT;
        case 'H': return K_HOME;  case 'F': return K_END;
        case '~': return num == 5 ? K_PGUP : num == 6 ? K_PGDN : num == 1 || num == 7 ? K_HOME
                       : num == 4 || num == 8 ? K_END : K_NONE;
      }
      return K_NONE;
    }
    in_drop(1);
    return K_CANCEL;
  }
  in_drop(1);
  if (c == '\r' || c == '\n') return K_OK;
  if (c == ' ') return K_APPLY;
  if (c == '\t') return K_TAB;
  return K_NONE;
}

// ---------------------------------------------------------------------------
// The mouse: every input device that moves relatively and has a left
// button, read as it comes. Looked for again every two seconds.
// ---------------------------------------------------------------------------
#define MAX_MICE 8
static int mice[MAX_MICE], nmice = 0, use_mouse = 1, debug = 0;
static long long mice_looked = 0;
static struct { int x, y, shown, down, clicked, released, wheel, fx, fy; } mouse = { CW / 2, CH / 2, 0, 0, 0, 0, 0, 0, 0 };

static long long now_ms(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#define BITS(n) (((n) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))
static int has_bit(const unsigned long *b, int n) { return (int)(b[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long))) & 1); }

static void mice_look(void) {
  if (!use_mouse || now_ms() - mice_looked < 2000) return;
  mice_looked = now_ms();
  for (int i = 0; i < nmice; i++) close(mice[i]);
  nmice = 0;
  DIR *d = opendir("/dev/input");
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)) && nmice < MAX_MICE) {
    if (strncmp(e->d_name, "event", 5)) continue;
    char path[300]; snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) continue;
    unsigned long rel[BITS(REL_MAX + 1)] = { 0 }, key[BITS(KEY_MAX + 1)] = { 0 };
    if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof rel), rel) >= 0 && ioctl(fd, EVIOCGBIT(EV_KEY, sizeof key), key) >= 0
        && has_bit(rel, REL_X) && has_bit(rel, REL_Y) && has_bit(key, BTN_LEFT)) {
      mice[nmice++] = fd;
      if (debug) fprintf(stderr, "mouse: %s\n", path);
    }
    else close(fd);
  }
  closedir(d);
}

// What the mice did since the last time. True if anything.
static int mice_read(void) {
  struct input_event ev;
  int any = 0;
  mice_look();
  for (int i = 0; i < nmice; i++) {
    while (read(mice[i], &ev, sizeof ev) == (ssize_t)sizeof ev) {
      if (debug && ev.type != EV_SYN) fprintf(stderr, "mouse event %d %d %d\n", ev.type, ev.code, ev.value);
      if (ev.type == EV_REL && (ev.code == REL_X || ev.code == REL_Y)) {
        // half a pixel of the canvas a count: about a screen's width for a hand's
        int *f = ev.code == REL_X ? &mouse.fx : &mouse.fy, *p = ev.code == REL_X ? &mouse.x : &mouse.y;
        *f += ev.value;
        *p += *f / 2; *f %= 2;
        if (mouse.x < 0) mouse.x = 0;
        if (mouse.x > CW - 1) mouse.x = CW - 1;
        if (mouse.y < 0) mouse.y = 0;
        if (mouse.y > CH - 1) mouse.y = CH - 1;
        mouse.shown = 1; any = 1;
      } else if (ev.type == EV_REL && ev.code == REL_WHEEL) {
        mouse.wheel -= ev.value; any = 1;
      } else if (ev.type == EV_KEY && ev.code == BTN_LEFT) {
        if (ev.value == 1) { mouse.down = 1; mouse.clicked = 1; }
        if (ev.value == 0) { mouse.down = 0; mouse.released = 1; }
        mouse.shown = 1; any = 1;
      }
    }
  }
  return any;
}

static int in(int x, int y, int w, int h) {
  return mouse.shown && mouse.x >= x && mouse.x < x + w && mouse.y >= y && mouse.y < y + h;
}

// ---------------------------------------------------------------------------
// What every screen has: the title, its line, the buttons.
// ---------------------------------------------------------------------------
static void header(const char *what) {
  int x = text(12, 6, "Hybrid Ports", C_WHITE);
  text(x + 6, 6, what, C_DIM);
  fill(12, 21, CW - 24, 1, C_PINK);
}

// A button, its right edge at xr; returns its left edge
static int button(int xr, int y, const char *label, int primary, int hover) {
  int w = text_w(label) + 12, x = xr - w;
  rbox(x, y, w + 1, 15, 3, primary ? C_PINK : hover ? C_HOVER : C_BG, 0);
  if (!primary) rbox(x, y, w + 1, 15, 3, C_DIM, 1);
  else rbox(x, y, w + 1, 15, 3, C_PINK, 1);
  text(x + 6, y + 1, label, C_WHITE);
  return x;
}

static void tickbox(int x, int y, int state, int box_colour) {
  frame(x, y + 2, 9, 9, box_colour);
  if (state == 2) { fill(x + 1, y + 3, 7, 7, C_PINK); text(x + 1, y, "✓", C_WHITE); }
  else if (state == 1) fill(x + 2, y + 5, 5, 3, C_PINK);
}

// ---------------------------------------------------------------------------
// main: the ports.
// ---------------------------------------------------------------------------
#define MAX_ROWS 256
typedef struct { char engine[64], game[64], label[128]; int tick; } Game;
typedef struct { int is_engine, first, count; } Row;   // a port (its games: first, count) or a game
static Game games[MAX_ROWS];
static int ngames = 0;
static char set_key[2][64], set_label[2][64];
static int set_tick[2], nsets = 0;
static Row rows[MAX_ROWS];
static int nrows = 0;

#define LIST_TOP 60
#define LIST_ROWS 8
#define SB_X (CW - 20)            // the scroll bar, 9 wide
#define SB_Y0 (LIST_TOP - 1)
#define SB_Y1 (LIST_TOP + LIST_ROWS * ROW - 2)
#define BUTTONS_Y (CH - 20)

static int engine_state(const Row *r) {
  int on = 0;
  for (int i = 0; i < r->count; i++) on += games[r->first + i].tick;
  return on == 0 ? 0 : on == r->count ? 2 : 1;
}
static void toggle(const Row *r) {
  if (r->is_engine) {
    int to = engine_state(r) != 2;
    for (int i = 0; i < r->count; i++) games[r->first + i].tick = to;
  } else games[r->first].tick = !games[r->first].tick;
}

static int read_table(const char *path) {
  FILE *f = fopen(path, "r");
  char line[512];
  if (!f) return -1;
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    char *t[5] = { 0 }; int n = 0;
    for (char *p = line; n < 5; ) { t[n++] = p; p = strchr(p, '\t'); if (!p) break; *p++ = 0; }
    if (n == 4 && !strcmp(t[0], "S") && nsets < 2) {
      snprintf(set_key[nsets], sizeof set_key[0], "%s", t[1]);
      set_tick[nsets] = atoi(t[2]) != 0;
      snprintf(set_label[nsets], sizeof set_label[0], "%s", t[3]);
      nsets++;
    } else if (n == 5 && !strcmp(t[0], "G") && ngames < MAX_ROWS) {
      Game *g = &games[ngames++];
      snprintf(g->engine, sizeof g->engine, "%s", t[1]);
      snprintf(g->game, sizeof g->game, "%s", t[2]);
      g->tick = atoi(t[3]) != 0;
      snprintf(g->label, sizeof g->label, "%s", t[4]);
    }
  }
  fclose(f);
  // the rows: a port, then its games, also the only one (what it is)
  for (int i = 0; i < ngames && nrows < MAX_ROWS - 1; ) {
    int j = i;
    while (j < ngames && !strcmp(games[j].engine, games[i].engine)) j++;
    rows[nrows++] = (Row){ 1, i, j - i };
    for (int k = i; k < j && nrows < MAX_ROWS; k++) rows[nrows++] = (Row){ 0, k, 1 };
    i = j;
  }
  return 0;
}

static int write_table(const char *path) {
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  for (int i = 0; i < nsets; i++) fprintf(f, "S\t%s\t%d\t%s\n", set_key[i], set_tick[i], set_label[i]);
  for (int i = 0; i < ngames; i++)
    fprintf(f, "G\t%s\t%s\t%d\t%s\n", games[i].engine, games[i].game, games[i].tick, games[i].label);
  return fclose(f);
}

// the scroll bar's handle: where and how long, for the first row shown
static void handle_of(int top, int *hy, int *hh) {
  int t0 = SB_Y0 + 8, t1 = SB_Y1 - 8, span = t1 - t0;
  *hh = nrows <= LIST_ROWS ? span : span * LIST_ROWS / nrows;
  if (*hh < 14) *hh = 14;
  int most = nrows - LIST_ROWS;
  *hy = t0 + (most > 0 ? (span - *hh) * top / most : 0);
}

static void draw_main(int focus_list, int sel_set, int sel, int top, int drag) {
  memset(cv, C_BG, sizeof cv);
  header("by danik");
  text(CW - 12 - text_w("v3"), 6, "v3", C_DIM);
  for (int i = 0; i < nsets; i++) {
    int y = 26 + i * ROW;
    if (!focus_list && sel_set == i) fill(8, y - 1, CW - 16, 15, C_BAR);
    tickbox(12, y, set_tick[i] ? 2 : 0, C_WHITE);
    text(26, y, set_label[i], C_WHITE);
  }
  fill(12, 56, CW - 24, 1, C_DARK);
  for (int i = 0; i < LIST_ROWS && top + i < nrows; i++) {
    const Row *r = &rows[top + i];
    int y = LIST_TOP + i * ROW, x0 = r->is_engine ? 12 : 26;
    if (top + i == sel) {
      if (focus_list) fill(8, y - 1, SB_X - 10, 15, C_BAR);
      else frame(8, y - 1, SB_X - 10, 15, C_BAR);
    }
    tickbox(x0, y, r->is_engine ? engine_state(r) : (games[r->first].tick ? 2 : 0), r->is_engine ? C_WHITE : C_DIM);
    text_cut(x0 + 14, y, r->is_engine ? games[r->first].engine : games[r->first].label, SB_X - 6 - x0 - 14, C_WHITE);
  }
  // the scroll bar (style 6): white triangles, a dark track, a pink handle with grips
  for (int r = 0; r < 4; r++) {
    fill(SB_X + 1 + 3 - r, SB_Y0 + 1 + r, 1 + 2 * r, 1, C_WHITE);
    fill(SB_X + 1 + r, SB_Y1 - 4 + r, 7 - 2 * r, 1, C_WHITE);
  }
  rbox(SB_X + 1, SB_Y0 + 8, 7, SB_Y1 - SB_Y0 - 15, 3, C_DARK, 0);
  int hy, hh;
  handle_of(top, &hy, &hh);
  rbox(SB_X + 1, hy, 7, hh + 1, 3, C_PINK, 0);
  for (int g = -3; g <= 3; g += 3) fill(SB_X + 3, hy + hh / 2 + g, 3, 1, C_BAR);
  (void)drag;
  // the bottom row
  text(12, BUTTONS_Y + 1, "↑↓←→ move  A tick", C_DIM);
  int xe = button(CW - 12, BUTTONS_Y, "B: Exit", 0, in(CW - 12 - text_w("B: Exit") - 12, BUTTONS_Y, text_w("B: Exit") + 13, 15));
  button(xe - 6, BUTTONS_Y, "X: Apply", 1, 0);
  if (mouse.shown) pointer(mouse.x, mouse.y);
}

static int screen_main(const char *table, const char *out) {
  if (read_table(table)) { fprintf(stderr, "danik_hybrid_ports_ui: cannot read %s\n", table); return 2; }
  int focus_list = nrows > 0, sel_set = 0, sel = 0, top = 0, drag = -1, result = -1;
  int w_exit = text_w("B: Exit") + 13, w_apply = text_w("X: Apply") + 13;
  int x_exit = CW - 12 - w_exit + 1, x_apply = x_exit - 6 - w_apply + 1;
  long long last = 0;
  int dirty = 1;
  while (result < 0) {
    int k = key_read();
    if (k == K_EOF && !in_tty) { result = 13; break; }
    int moved = mice_read();
    if (k == K_NONE && !moved && !dirty) {
      struct pollfd p = { 0, POLLIN, 0 };
      poll(&p, 1, nmice ? 15 : 50);
      if (now_ms() - last < 1000) continue;
    }
    dirty = 0;
    int most = nrows > LIST_ROWS ? nrows - LIST_ROWS : 0;
    switch (k) {
      case K_UP:   if (focus_list) { if (sel > 0) sel--; } else if (sel_set > 0) sel_set--; break;
      case K_DOWN: if (focus_list) { if (sel < nrows - 1) sel++; } else if (sel_set < nsets - 1) sel_set++; break;
      case K_LEFT: focus_list = 0; break;
      case K_RIGHT: if (nrows) focus_list = 1; break;
      case K_PGUP: if (focus_list) { sel -= LIST_ROWS; if (sel < 0) sel = 0; } break;
      case K_PGDN: if (focus_list) { sel += LIST_ROWS; if (sel > nrows - 1) sel = nrows - 1; } break;
      case K_HOME: if (focus_list) sel = 0; break;
      case K_END:  if (focus_list) sel = nrows - 1; break;
      case K_OK:   if (focus_list) { if (nrows) toggle(&rows[sel]); } else set_tick[sel_set] = !set_tick[sel_set]; break;
      case K_APPLY: result = 0; break;
      case K_CANCEL: result = 10; break;
    }
    // keys: the line chosen stays in view
    if (k != K_NONE) {
      if (sel < top) top = sel;
      if (sel >= top + LIST_ROWS) top = sel - LIST_ROWS + 1;
    }
    // the mouse
    if (mouse.wheel) { top += mouse.wheel * 2; mouse.wheel = 0; }
    if (mouse.clicked) {
      mouse.clicked = 0;
      int hy, hh;
      handle_of(top, &hy, &hh);
      if (in(x_apply, BUTTONS_Y, w_apply, 15)) result = 0;
      else if (in(x_exit, BUTTONS_Y, w_exit, 15)) result = 10;
      else if (in(SB_X - 2, SB_Y0 - 2, 13, 10)) top--;
      else if (in(SB_X - 2, SB_Y1 - 7, 13, 10)) top++;
      else if (in(SB_X - 2, hy, 13, hh)) drag = mouse.y - hy;
      else if (in(SB_X - 2, SB_Y0 + 8, 13, hy - SB_Y0 - 8)) top -= LIST_ROWS;
      else if (in(SB_X - 2, hy + hh, 13, SB_Y1 - 8 - hy - hh)) top += LIST_ROWS;
      else {
        for (int i = 0; i < nsets; i++)
          if (in(8, 26 + i * ROW - 1, SB_X - 10, ROW)) { focus_list = 0; sel_set = i; set_tick[i] = !set_tick[i]; }
        for (int i = 0; i < LIST_ROWS && top + i < nrows; i++)
          if (in(8, LIST_TOP + i * ROW - 1, SB_X - 10, ROW)) { focus_list = 1; sel = top + i; toggle(&rows[sel]); }
      }
    }
    if (drag >= 0 && mouse.down) {
      int t0 = SB_Y0 + 8, t1 = SB_Y1 - 8, hy, hh;
      handle_of(0, &hy, &hh);
      int span = t1 - t0 - hh;
      if (span > 0) top = ((mouse.y - drag - t0) * most + span / 2) / span;
    }
    if (mouse.released) { mouse.released = 0; drag = -1; }
    if (top > most) top = most;
    if (top < 0) top = 0;
    draw_main(focus_list, sel_set, sel, top, drag >= 0);
    fb_present();
    last = now_ms();
  }
  if (result == 0 || result == 10) if (write_table(out)) result = 2;
  return result;
}

// ---------------------------------------------------------------------------
// run: a command's output.
// ---------------------------------------------------------------------------
#define OUT_LINES 10
#define OUT_COLS 48
static char outl[OUT_LINES][OUT_COLS * 4 + 1];
static int nout = 0, outcol = 0;

static void out_newline(void) {
  if (nout < OUT_LINES) nout++;
  else memmove(outl[0], outl[1], sizeof outl[0] * (OUT_LINES - 1));
  outl[nout - 1][0] = 0;
  outcol = 0;
}
// the command's bytes: lines kept, colours and cursor moves dropped, \r
// starting the line again (progress), long lines cut
static void out_bytes(const char *b, ssize_t n) {
  static int esc = 0;
  if (nout == 0) out_newline();
  for (ssize_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)b[i];
    if (esc) { if ((c >= '@' && c <= '~' && c != '[') || esc > 16) esc = 0; else esc++; continue; }
    if (c == 0x1b) { esc = 1; continue; }
    if (c == '\n') { out_newline(); continue; }
    if (c == '\r') { outl[nout - 1][0] = 0; outcol = 0; continue; }
    if (c == '\t') c = ' ';
    if (c < 32) continue;
    size_t len = strlen(outl[nout - 1]);
    if (outcol < OUT_COLS && len < sizeof outl[0] - 1) {
      outl[nout - 1][len] = (char)c; outl[nout - 1][len + 1] = 0;
      if ((c & 0xC0) != 0x80) outcol++;
    }
  }
}

static void draw_run(const char *title, int done, int code, long long t, int close_hover) {
  memset(cv, C_BG, sizeof cv);
  header(!done ? "applying" : code == 0 ? "done" : "failed");
  (void)title;
  rbox(8, 28, CW - 16, CH - 58, 3, C_DARK, 0);
  // the last lines; the last one, empty while the next comes, not shown
  int n = nout, from = 0;
  if (n > 0 && outl[n - 1][0] == 0) n--;
  if (n > OUT_LINES) from = n - OUT_LINES;
  for (int i = from; i < n; i++) text_cut(14, 32 + (i - from) * 13, outl[i], CW - 28, C_TEXT2);
  if (!done) {
    rbox(12, BUTTONS_Y + 3, CW - 24, 9, 3, C_DIM, 1);
    int span = CW - 24 - 4 - 50, pos = (int)(t / 8 % (2 * span));
    if (pos > span) pos = 2 * span - pos;
    fill(14 + pos, BUTTONS_Y + 5, 50, 5, C_PINK);
  } else {
    char msg[64];
    if (code == 0) snprintf(msg, sizeof msg, "Load a port from Other.");
    else snprintf(msg, sizeof msg, "It did not work (exit code %d).", code);
    text(12, BUTTONS_Y + 1, msg, C_DIM);
    button(CW - 12, BUTTONS_Y, "A: Close", 1, close_hover);
  }
  if (mouse.shown) pointer(mouse.x, mouse.y);
}

static int screen_run(const char *title, char **argv) {
  int pipefd[2];
  if (pipe(pipefd)) return 2;
  pid_t pid = fork();
  if (pid < 0) return 2;
  if (pid == 0) {
    dup2(pipefd[1], 1); dup2(pipefd[1], 2);
    close(pipefd[0]); close(pipefd[1]);
    int null = open("/dev/null", O_RDONLY);
    if (null >= 0) { dup2(null, 0); close(null); }
    execvp(argv[0], argv);
    fprintf(stderr, "cannot run %s: %s\n", argv[0], strerror(errno));
    _exit(127);
  }
  close(pipefd[1]);
  fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
  int done = 0, code = 0, open_ = 1;
  long long start = now_ms();
  char buf[4096];
  while (!done) {
    struct pollfd p = { pipefd[0], POLLIN, 0 };
    if (open_ && poll(&p, 1, 30) > 0) {
      ssize_t n = read(pipefd[0], buf, sizeof buf);
      if (n > 0) out_bytes(buf, n);
      else if (n == 0) open_ = 0;
    }
    if (!open_) {
      int st;
      if (waitpid(pid, &st, 0) == pid) { code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st); done = 1; }
    }
    mice_read();
    while (key_read() != K_NONE && !in_eof) { }   // keys while it works are not kept
    draw_run(title, 0, 0, now_ms() - start, 0);
    fb_present();
  }
  // the end: until A, B, Enter, Escape or a click on Close
  int w = text_w("A: Close") + 13, x = CW - 12 - w + 1;
  for (;;) {
    mice_read();
    int k = key_read();
    if (k == K_OK || k == K_CANCEL || (k == K_EOF && !in_tty)) break;
    if (mouse.clicked) { mouse.clicked = 0; if (in(x, BUTTONS_Y, w, 15)) break; }
    draw_run(title, 1, code, 0, in(x, BUTTONS_Y, w, 15));
    fb_present();
    struct pollfd p = { 0, POLLIN, 0 };
    poll(&p, 1, 20);
  }
  return code;
}

// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
  const char *cmd = argc > 1 ? argv[1] : "", *table = NULL, *out = NULL, *title = "", *dev = "/dev/fb0", *geometry = NULL;
  int i = 2;
  for (; i < argc; i++) {
    if (!strcmp(argv[i], "--table") && i + 1 < argc) table = argv[++i];
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "--title") && i + 1 < argc) title = argv[++i];
    else if (!strcmp(argv[i], "--fb") && i + 1 < argc) dev = argv[++i];
    else if (!strcmp(argv[i], "--geometry") && i + 1 < argc) geometry = argv[++i];
    else if (!strcmp(argv[i], "--keep")) keep = 1;
    else if (!strcmp(argv[i], "--no-mouse")) use_mouse = 0;
    else if (!strcmp(argv[i], "--debug")) debug = 1;
    else if (!strcmp(argv[i], "--")) { i++; break; }
    else { fprintf(stderr, "danik_hybrid_ports_ui: what is %s?\n", argv[i]); return 2; }
  }
  if (!strcmp(cmd, "release")) { tty_graphics = 1; keep = 0; in_tty = isatty(0); tty_restore(); return 0; }
  if (fb_open(dev, geometry)) return 12;
  if (!strcmp(cmd, "probe")) return 0;
  tty_setup();
  int r;
  if (!strcmp(cmd, "main") && table && out) r = screen_main(table, out);
  else if (!strcmp(cmd, "run") && i < argc) r = screen_run(title, argv + i);
  else { tty_restore(); fprintf(stderr, "usage: danik_hybrid_ports_ui main --table FILE --out FILE | run -- COMMAND...\n"); return 2; }
  tty_restore();
  return r;
}
