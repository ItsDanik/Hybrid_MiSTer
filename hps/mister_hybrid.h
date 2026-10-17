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
#define MH_MAX_WIDTH 640
#define MH_MAX_HEIGHT 480

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
// The game does not choose between the forms and a 15kHz screen never gets
// anything else.
//
// 800x600 and 1024x768 (5 and 6) were there with host versions 6 to 12, at
// 31kHz and above only. They are gone: MH_ModeAvailable() returns 0 for them.
// Their names are left for code that still has them in a list.
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
// "O[15],FPS Counter,Off,On;": the library draws the number of frames shown in
// the last second in the top left corner of every frame, the game does nothing
#define MH_OSD_FPS(s) ((int)(((s) >> 15) & 1))
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
// The name Main_MiSTer gives the settings (config/<name>.CFG) and button
// mappings (config/inputs/<name>_input_*) of what was started: the set name
// of the menu entry (an MGL with <setname>ECWolf_SOD</setname>: "ECWolf_SOD"),
// or the core's name (MISTER_HYBRID_CORE). From /tmp/CORENAME, or
// MISTER_HYBRID_SET in the environment. Read once.
const char* MH_SetName(void);
// The name Main_MiSTer has saved the button mappings under: the set name in
// /tmp/CORENAME (what was loaded), else MH_SetName(). Read anew each call.
// Not MISTER_HYBRID_SET, which says which game to start and can differ
// (tools/test_on_mister.sh loads <Engine>_TEST and starts <Engine>_<game>).
const char* MH_MappingName(void);
// The host version of the core (hybrid_host.sv), 0 without one
int MH_HostVersion(void);

// Video. The core shows its own picture (the MiSTer logo) until the first
// frame, plays its intro to the end and fades it out before that frame is
// shown: the call that shows it (MH_Present(), or MH_SetPalette() with 8bpp)
// returns when the game has the screen, 0.7 to 4.8 seconds later.
// A change of format later on blanks the screen until the next frame.
void MH_SetFormat(int format);
// 1 if the core can show the mode: it is new enough (and it is not 800x600 or
// 1024x768, which no core has any more).
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
// The same without MH_Present()'s copy of the frame (a tenth of a frame's
// time for a 320x200 game on the MiSTer): MH_FrameBuffer() is the buffer the
// next frame is to be drawn into, MH_Width() x MH_Height() with rows of
// MH_Width() pixels, and MH_PresentBuffer() shows it. Draw all of it: it holds
// the frame before last. MH_FrameBuffer() waits while the two frames before
// are still on their way to the core, and returns NULL where the library has
// no buffers of its own (no second CPU): then use MH_Present(). An
// MH_Present() in between takes the buffer back.
void* MH_FrameBuffer(void);
void MH_PresentBuffer(void);
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
// Show the next frame presented (MH_Present(), MH_PresentBuffer()) at field
// counter `field`, not before: the library holds it until the field before.
// For drawing a frame ahead, so that one that takes a little longer than a
// field is still shown in time: start a frame for field F once the counter
// is F - 2, and present it with MH_PresentAtField(F). The game's time steps
// from one F to the next. Not with an interlaced mode (MH_Interlaced()):
// there frames are shown at once. Without it, or with 0, as soon as possible.
// MISTER_HYBRID_AHEAD=0 in the game's environment makes it do nothing: the
// frame drawn from F - 2 is shown at F - 1, as without drawing ahead, which
// is how to compare the two (hybrid/CLAUDE.md).
void MH_PresentAtField(uint32_t field);
// 1 while the mode is shown interlaced (640x400 and 640x480 on a 15kHz screen)
int MH_Interlaced(void);
// Bits 0..3 that hide or grey out lines of the core's OSD ("H0", "D1", ... in
// its CONF_STR): for options the game cannot honour with the data it has
void MH_SetMenuMask(int mask);

// Input
void MH_ReadInput(mh_input* input);
uint64_t MH_OSDStatus(void);
// OSD triggers: lines "T[56],<label>;" .. "T[59],<label>;" of the core's
// CONF_STR, which do something once each time the player selects them (a
// cheat, "next level"). Bit n set: trigger 56 + n was selected since the
// last call (host version 13: the core turns the trigger's short pulse into
// a toggle in bits 56..59 of MH_OSDStatus(), which this follows). 0 without
// such a core. Call it from one place, every frame.
int MH_OSDTriggers(void);
// OSD status bits [127:64]: the core's CRT Options page (host version 13; 0
// with an older core)
uint64_t MH_OSDStatusHigh(void);

// HUD margins (hybrid/README.md, "HUD margins"): how many pixels the player
// wants between the game's HUD and the top / bottom edge of the screen, 0..24,
// for a 240-line screen (twice that in a 480-line mode), from the OSD's CRT
// Options. A game that draws its view in 240 lines puts its top HUD that far
// from the top and its bottom HUD that far from the bottom, every frame, so a
// change shows at once. A screen with overscan hides the edges; HDMI has none.
// Without the core's lines for it (an older core) both are MH_HUD_DEFAULT.
#define MH_HUD_DEFAULT 10
#define MH_HUD_MAX 24
void MH_HudMargins(int* top, int* bottom);
// Core buttons (MH_JOY_BUTTON bits) driven by the physical buttons the OSD's
// Menu OK / Menu Back options name. MiSTer cannot map one physical button to
// two core buttons, so this is how a button with a game function can also
// confirm and cancel in menus. Cheap to call every frame.
void MH_MenuButtons(uint64_t osd_status, uint32_t* ok_bits, uint32_t* back_bits);
// Whether Menu OK / Menu Back are pressed now, `joystick` the core's buttons
// of player 1: through the core buttons of MH_MenuButtons(), or the physical
// button read from Linux, for one that drives no core button (a button
// without a function in this core: MiSTer passes it to the core not at all).
// Cheap to call every frame.
void MH_MenuButtonsPressed(uint64_t osd_status, uint32_t joystick, int* ok, int* back);
// The gamepad's analog triggers (L2, R2), 0 = released .. 255 = fully pressed.
// MiSTer does not pass them to a core, so they are read from Linux directly
// (mister_triggers.c) and are those of whichever controller is pressed
// furthest: for player 1 of a game one plays alone. A racing wheel Main_MiSTer
// knows gives its pedals here: the gas as the right trigger, the brake as the
// left (taken from the core's sticks, where Main_MiSTer puts them). Returns 0
// while no controller with triggers (or such a wheel) is connected. Cheap to
// call every frame.
int MH_ReadTriggers(int* left, int* right);
// The clicks of the left and right stick (L3, R3), 1 while pressed, which
// MiSTer's default map does not pass on either: read the same way, from the
// gamepads' BTN_THUMBL and BTN_THUMBR (the standard gamepad buttons; a
// controller whose driver names them otherwise has none here), pressed on
// any controller. Returns 0 while no gamepad is connected.
int MH_ReadStickButtons(int* left, int* right);
// Rumble: the strength of the gamepad's heavy (slow) and light (fast) motor,
// 0 = off .. 255, on every connected controller that can. It goes on at that
// strength until the next call, but a game must repeat the call at least
// every second or two while it wants rumble (once a frame is fine and costs
// nothing): left alone the motors stop by themselves after two seconds.
// Returns 0 while no controller that can rumble is connected.
int MH_Rumble(int strong, int weak);
// USB HID usage (= SDL scancode) of a key bitmap index, 0 if there is none
int MH_KeyToHID(int key_index);

// Audio: stereo frames {R << 16 | L}, played by the core at MH_AUDIO_RATE
void MH_AudioEnable(int enabled);
// Blocks until the core has less than `lead` frames left to play, then queues
// `count` frames. The core's sample clock paces the caller.
void MH_AudioWrite(const uint32_t* frames, int count, int lead);

// FM chip: a YM3812 (OPL2, the AdLib card's chip) in the core, for games that
// made their music with one. The game sends the chip's register writes and
// the core adds the chip's sound to the audio; nothing is worked out on the
// ARM. A host older than version 10 has no chip: MH_FMAvailable() is 0 and the
// calls below do nothing, so a game keeps its own way of making the music for
// that case (or needs the newer core).
#define MH_FM_RING_WRITES 4096
int MH_FMAvailable(void);
// Needs MH_AudioEnable(1) first. Enabling resets the chip and writes zero to
// all its registers; disabling silences it (and takes two fields).
void MH_FMEnable(int enabled);
// 0..127: 32 is the chip as it comes, 127 nearly four times that (held at
// the limits)
void MH_FMVolume(int volume);
// Write `value` to register `reg` (0..255) of the chip. The write is made
// when audio frame `ahead` of the block the game will queue NEXT with
// MH_AudioWrite() is played (0 = with its first frame), so music made inside
// an audio callback keeps its timing to the sample: pass the position in the
// block being filled, in 44.1kHz frames. Call it from the thread that calls
// MH_AudioWrite(), or with 0 from anywhere. If the core has 4096 writes
// waiting (more than a second of the densest music), the write is dropped.
void MH_FMWrite(int reg, int value, int ahead);

// Messages: shown by the core under its logo, on its own picture (the purple
// screen), host version 13. MH_MSG_ROWS lines of MH_MSG_COLS characters
// (32..126; others are shown as '?') is all there is room for: write
// messages to fit, say what is wrong and what to do (which files go where),
// in plain words. Longer text is cut, its last line ending in "...", and the
// whole of it is in the log.
#define MH_MSG_COLS 71
#define MH_MSG_ROWS 4
#define MH_MSG_ERROR 1 // the dots under the logo stand still
// Show a message. The text is wrapped at spaces and at '\n'; empty lines are
// left out (4 lines are few). A game that has the screen gives it back to
// the core, which shows its picture with the message; the game takes the
// screen again with its next frame, and that clears the message. Returns
// the number of lines the text needed. Works without the game having set up
// anything, and from another program (tools/mh_message.c).
int MH_ShowMessage(const char* text, int flags);
void MH_ClearMessage(void);
// A list to pick from, in the place of a message: the MH_MSG_ROWS items from
// `top`, the one `selected` on a bar, a mark in the last column where there
// are more above or below (items are cut at MH_MSG_COLS - 2 characters).
// MH_UI_Menu() is the whole of it, with the player's input.
void MH_ShowList(const char* const* items, int count, int top, int selected);
// 1 while a message is shown, 2 while the core shows its own (no game came
// in 12 seconds after it was loaded), 0 else
int MH_MessageShown(void);

// A core with several games (mister_games.c; hybrid/README.md, "Several games
// in one core"). Each game has a menu entry of its own, an MGL with the set
// name <core>_<id>; a shareware or demo version has none.
typedef struct mh_game {
    const char* id;           // its menu entry's set name after "<core>_": "SOD" of ECWolf_SOD
    const char* name;         // in the list: "Spear of Destiny"
    const char* const* files; // all there when the game is: paths in the game's folder, NULL at
                              // the end; * and ? and any case ("*.SOD", "addons/WT.GRP")
    const char* shareware_of; // the id of the full game this is the shareware or demo of, or NULL
    const char* missing;      // the message (4 lines of 71) when its menu entry was started and
                              // it is not there: which files to copy where, %s for the folder
} mh_game;
// `files` of a game the port looks for by itself (by checksum, as jfSW does)
// and did not find: never there, whatever is in the folder. (NULL is always
// there: one it did find.)
extern const char* const MH_NOT_THERE[];
// The game's folder as the player sees it on the SD card ("games/dnk/ECWolf"):
// the current one, for messages
const char* MH_GamePath(void);
// 1 if the game's files are in `dir` (NULL: the current folder)
int MH_GameFound(const mh_game* game, const char* dir);
// Which game to start. From a game's menu entry: that game; its shareware
// when the full game is not there; else -1 after its `missing` message. From
// the core itself (Recent, Zaparoo, a bootcore, the core's .rbf): of the
// games that are there (a shareware on a line of its own, also next to its
// full game) the only one, or the one the player picks from a list
// (*listed = 1); -1 when the player backs out, or after the message `none`
// when there is none. A message waits until the player leaves the core; %s
// in it is the game's folder (MH_GamePath()).
int MH_PickGame(const mh_game* games, int count, const char* dir, const char* none, int* listed);

// Screens every hybrid core shares (mister_ui.c), drawn straight to the core;
// they work before the game has set up anything.
// A list to pick from, with the keyboard or joystick 1. Returns the index of
// the chosen item, or -1 for Back/Esc or when the core is gone. Host version
// 13 and later: under the core's logo (MH_ShowList(), 4 items at a time; the
// title and the prompt only go to the log), the list stays until the next
// message or the game's first frame; Back clears it. Before: a screen of its
// own, 320x200.
int MH_UI_Menu(const char* title, const char* prompt, const char* const* items, int count, int selected);
#define MH_UI_WAIT 1  // until the player leaves the core (host 12 and older: a key or button)
#define MH_UI_ERROR 2 // an error: the dots under the logo stand still
// A message (MH_ShowMessage(); the title goes to the log only, before host
// version 13 it was a title bar). With MH_UI_WAIT it returns once the core
// has been left: the message stays until then, whatever the launcher does.
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
