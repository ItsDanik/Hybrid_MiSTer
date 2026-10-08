#ifndef MISTER_HYBRID_H
#define MISTER_HYBRID_H

// HPS side of a MiSTer hybrid core: the game runs on the ARM and talks to the
// FPGA core (hybrid/rtl/hybrid_host.sv, which documents the memory layout)
// through shared DDR3 memory. The core scans out the game's frames at 15kHz
// (320x200 and the other video modes below), plays a 44.1kHz audio
// ring and publishes keyboard, mouse, joystick and OSD state.
//
// Environment variables:
//   MISTER_HYBRID_CORE  name of the core as in /tmp/CORENAME (the name in its
//                       CONF_STR). Set by the launcher; MH_Open() only attaches
//                       to this core and MH_CheckAlive() notices when another
//                       one is loaded. It is also the prefix of the core's
//                       joystick mapping files.
//   MISTER_HYBRID_JN    the core's default button mapping, the "jn," list of
//                       its CONF_STR ("A,B,X,Y,L,R,Select,Start" if unset)
//   MISTER_HYBRID_SHM   development: a file to use instead of the DDR3 window
//                       (see hybrid/tools/fakecore.c)

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The video mode after MH_Open(), and the size of the shared screens
#define MH_WIDTH 320
#define MH_HEIGHT 200
// The largest video mode
#define MH_MAX_WIDTH 1024
#define MH_MAX_HEIGHT 768

// Video modes. The picture fills the same 4:3 screen in all of them.
//
// 320x200, 640x200, 320x240 and 640x240 are 15.6kHz at 59.6Hz. 640x200 has
// two pixels in the place of each pixel of 320x200, and 640x240 of 320x240.
//
// 640x400 and 640x480 are for games that have nothing smaller. The core shows
// them interlaced at 15kHz: a frame is on the screen for two fields, its even
// rows in the first and its odd rows in the second (29.8 frames per second;
// fine horizontal lines flicker on a CRT, the HDMI scaler weaves the fields
// into a steady picture). They are progressive at 31kHz instead where the
// player says that no 15kHz screen is there to get it: the analog output is a
// VGA monitor (forced_scandoubler=1 in MiSTer.ini) or is not used at all (the
// OSD option "HDMI Only", which switches it off while such a mode is shown).
//
// 800x600 and 1024x768 (60Hz) have no 15kHz form and exist only in that case:
// see MH_ModeAvailable().
//
// The game does not choose between the forms and a 15kHz screen never gets
// anything else.
#define MH_MODE_320x200 0
#define MH_MODE_640x200 1
#define MH_MODE_640x400 2
#define MH_MODE_320x240 3
#define MH_MODE_640x480 4
#define MH_MODE_800x600 5
#define MH_MODE_1024x768 6
#define MH_MODE_640x240 7
#define MH_MODE_COUNT 8

#define MH_FORMAT_INDEX8 0 // 8bpp, palette from MH_SetPalette()
#define MH_FORMAT_RGB565 1

#define MH_AUDIO_RATE 44100
#define MH_AUDIO_RING_FRAMES 16384

// MiSTer joystick word: directions, then the buttons of the core's "J1," list
#define MH_JOY_RIGHT (1u << 0)
#define MH_JOY_LEFT (1u << 1)
#define MH_JOY_DOWN (1u << 2)
#define MH_JOY_UP (1u << 3)
#define MH_JOY_BUTTON(n) (1u << (4 + (n)))

// OSD status bits every hybrid core lays out the same way (see
// hybrid/README.md). Bits 24..63 belong to the game. The first entry of an
// option is 0 and so is the default.
#define MH_OSD_SOUND_VOLUME(s) ((int)(((s) >> 7) & 0xF))  // 0 = 100%, 10 = 0%
#define MH_OSD_MUSIC_VOLUME(s) ((int)(((s) >> 11) & 0xF)) // 0 = 100%, 10 = 0%
// 0 = the MiSTer menu's OK/Back buttons, 1.. = A, B, X, Y, L, R, Select, Start
#define MH_OSD_MENU_OK(s) ((int)(((s) >> 16) & 0xF))
#define MH_OSD_MENU_BACK(s) ((int)(((s) >> 20) & 0xF))
#define MH_OSD_GAME_BITS(s, lsb, width) ((int)(((s) >> (lsb)) & ((1u << (width)) - 1)))

typedef struct mh_input {
    uint32_t frame;        // field counter, increments every vblank (59.6Hz)
    uint32_t joystick[2];  // MH_JOY_* bits
    int8_t analog_lx[2];   // left stick, -128..127
    int8_t analog_ly[2];
    int8_t analog_rx[2];   // right stick
    int8_t analog_ry[2];
    uint64_t osd_status;   // OSD option bits [63:0]
    int32_t mouse_x;       // accumulated PS/2 mouse movement (y positive up)
    int32_t mouse_y;
    int16_t mouse_wheel;   // accumulated wheel movement
    int mouse_buttons;     // bit 0 left, bit 1 right, bit 2 middle
    uint32_t keys[16];     // PS/2 set 2 key bitmap, bit index = extended << 8 | code
} mh_input;

// Returns 1 if the core is loaded and running. Without it everything below is
// a no-op, so a game can run headless.
int MH_Open(void);
void MH_Close(void);
int MH_IsOpen(void);
// Returns 0 (and detaches for good) once the core is no longer loaded, or was
// loaded again
int MH_CheckAlive(void);
// After MH_CheckAlive() returned 0: 1 if /tmp/CORENAME still names our core.
// Then it was loaded again (from the MiSTer menu, while the game ran) or is
// about to be replaced. The game should leave with exit code 43, for its
// launcher to start it again once the core has settled.
int MH_CoreReloaded(void);
const char* MH_CoreName(void);

// Video. The core shows its own picture (the MiSTer logo) until the first
// frame, and fades it out before that frame is shown: the call that shows it
// (MH_Present(), or MH_SetPalette() with 8bpp) returns 0.7 seconds later,
// when the game has the screen.
// A change of format later on blanks the screen until the next frame.
void MH_SetFormat(int format);
// 1 if the core can show the mode right now: it is new enough and, for
// 800x600 and 1024x768, the player has no 15kHz screen on it (see above).
// That can change while the game runs (an OSD option): a mode that is no
// longer available is shown as a black screen until the game sets another.
int MH_ModeAvailable(int mode);
// Size of a video mode
int MH_ModeWidth(int mode);
int MH_ModeHeight(int mode);
// Switch to another video mode; the screen is blank until the next frame.
// Returns 0 if the mode is not available
int MH_SetMode(int mode);
int MH_Mode(void);
int MH_Width(void);
int MH_Height(void);
// Show a frame of MH_Width() x MH_Height(). `pitch` is the source's row size
// in bytes
void MH_Present(const void* pixels, int pitch);
// 256 entries of 0x00RRGGBB
void MH_SetPalette(const uint32_t* rgb);
uint32_t MH_FieldCounter(void);
// Sleep until the field counter is past `field`
void MH_WaitField(uint32_t field);
// 1 once the core has taken the frame presented at field counter `field`: at
// the next vblank, with 640x400 interlaced the one after both fields of the
// frame before were shown. That is the moment to start drawing the next
// frame; a game that presents more often loses frames.
int MH_FrameTaken(uint32_t field);
// Sleep until MH_FrameTaken(field)
void MH_WaitFrame(uint32_t field);
// Bits 0..3 that hide or grey out lines of the core's OSD ("H0", "D1", ... in
// its CONF_STR): for options the game cannot honour with the data it has
void MH_SetMenuMask(int mask);

// Input
void MH_ReadInput(mh_input* input);
uint64_t MH_OSDStatus(void);
// Core buttons (MH_JOY_BUTTON bits) driven by the physical buttons the OSD's
// Menu OK / Menu Back options name. MiSTer cannot map one physical button to
// two core buttons, so this is how a button with a game function can also
// confirm and cancel in menus. Cheap to call every frame.
void MH_MenuButtons(uint64_t osd_status, uint32_t* ok_bits, uint32_t* back_bits);
// USB HID usage (= SDL scancode) of a key bitmap index, 0 if there is none
int MH_KeyToHID(int key_index);

// Audio: stereo frames {R << 16 | L}, played by the core at MH_AUDIO_RATE
void MH_AudioEnable(int enabled);
// Blocks until the core has less than `lead` frames left to play, then queues
// `count` frames. The core's sample clock paces the caller.
void MH_AudioWrite(const uint32_t* frames, int count, int lead);

// Screens every hybrid core shares (mister_ui.c), drawn straight to the core;
// they work before the game has set up anything.
// A list to pick from, with the keyboard or joystick 1. Returns the index of
// the chosen item, or -1 for Back/Esc or when the core is gone.
int MH_UI_Menu(const char* title, const char* prompt, const char* const* items, int count, int selected);
#define MH_UI_WAIT 1  // until a key or button is pressed
#define MH_UI_ERROR 2 // red title bar
// A message; lines wrap at 36 characters
void MH_UI_Message(const char* title, const char* text, int flags);

// Helper threads (audio, frame copy) belong on CPU1 when the launcher allows
// it (taskset 0x03): the game then has CPU0 to itself. Returns 0 if not allowed
int MH_PinThreadToCPU1(void);
// Load the MiSTer menu core. Call after MH_Close() when the player quits
void MH_QuitToMenu(void);

#ifdef __cplusplus
}
#endif

#endif
