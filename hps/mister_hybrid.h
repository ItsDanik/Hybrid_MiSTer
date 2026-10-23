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
//   MISTER_HYBRID_SOUNDFONTS  the folder of the SoundFonts, instead of
//                       games/dnk/SoundFonts (MH_SoundFontDir())

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
// Bits 16..23 were the options Menu OK / Menu Back up to host version 13 (a
// saved setting may still have them set): 16 is Save Diagnostic Report since
// host version 16, which the library reads itself (MH_DiagAdd()), 17..23 unused
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

// Input. While the OSD menu is open the game gets none: Main_MiSTer passes
// the core no buttons and keys then, and the library holds back the rest
// (sticks, mouse, and what it reads from Linux: triggers, stick clicks, the
// physical Menu OK / Back). What is held when the OSD closes (the button that
// closed it) stays released until it is let go.
void MH_ReadInput(mh_input* input);
// 1 while the OSD menu is open (host version 14; always 0 with an older core),
// not for its info boxes (volume, a controller connected). For a game that
// wants to pause meanwhile; nothing else needs it.
int MH_OSDOpen(void);
uint64_t MH_OSDStatus(void);
// Shows text n (1..255) of the core's "I,<text 1>,<text 2>,...;" line in
// Main_MiSTer's info box, the one of "ROMs decrypting", for 2 seconds; while
// the OSD menu is open the core holds it until the menu closes (host version
// 16; nothing with an older core). Texts 1..23 are the framework's (the
// template's I line): 1..13 the MT32-pi's, which the core shows itself,
// 14..16 MH_INFO_*; a game's own start at 24. A text has no commas or
// semicolons; "\n" breaks a line, 30 characters to a line.
void MH_OSDInfo(int n);
#define MH_INFO_NO_SOUNDFONT      14 // no .sf2 in games/dnk/SoundFonts/
#define MH_INFO_SOUNDFONT_DAMAGED 15 // the slot's file is no SoundFont 2, or damaged
#define MH_INFO_SOUNDFONT_TOO_BIG 16 // more samples than the 64 MB of sample memory
#define MH_INFO_DIAG_SAVED        17 // the diagnostic report is saved (docs/diagnostics.md)
#define MH_INFO_DIAG_FAILED       18 // it could not be
// The diagnostic report (docs/diagnostics.md): the OSD line "Save
// Diagnostic Report" saves the setup and the last 60 seconds in
// games/dnk/diagnostics/ (host version 16), with no code in the game. A game
// that knows more about its own state (its settings, the level) may add a
// file to it: `text` as the report's game/<name>, the last text given for a
// name (copied; NULL takes it out). At most 8.
void MH_DiagAdd(const char* name, const char* text);
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
// change shows at once. A screen with overscan hides the edges; HDMI has none:
// 0 by default (Danik, 2026-10-21: any other default confused players without
// overscan), a CRT's player moves them in. Without the core's lines for it (an
// older core) both are MH_HUD_DEFAULT.
#define MH_HUD_DEFAULT 0
#define MH_HUD_MAX 24
void MH_HudMargins(int* top, int* bottom);
// Core buttons (MH_JOY_BUTTON bits) driven by the physical buttons of the
// MiSTer menu's OK and Back: the controller's Menu OK / Menu Back of "Define
// buttons", else its A / B; without a mapping the buttons its console
// confirms and goes back with (Nintendo: A right, B bottom; PlayStation,
// Xbox and the others: the bottom one confirms, the right one goes back).
// MiSTer cannot map one physical button to two core buttons, so this is how a
// button with a game function can also confirm and cancel in menus.
// `osd_status` is not used any more (it held the options up to host version
// 13). Cheap to call every frame.
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
// 1 while a racing wheel Main_MiSTer knows (as above) is connected. Main_MiSTer
// makes it whichever player it comes as, often player 2 next to a gamepad, so
// a game played alone takes both players' controllers together then (the SDL
// driver does, for its joystick 1). Cheap to call every frame.
int MH_WheelConnected(void);
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
// Force feedback (a racing wheel, or a joystick that has it): Linux force
// feedback with constant forces (FF_CONSTANT) on every connected controller
// that can; Main_MiSTer passes a core none. Effects are in slots of the
// game's choosing, 0..MH_FORCE_SLOTS-1, a slot one effect, played together.
// A level is -255 (to the left) .. 255 (to the right). An effect with no end
// (ms = 0) runs until set again or stopped, but a game must call
// MH_ForceKeep() at least every second while it wants it (once a frame is
// fine): left alone it stops by itself after two seconds, so that a game that
// went away leaves no wheel pulling. Written without a wheel to try it on
// (2026-10-21, for ReNFS): MISTER_HYBRID_FORCE_INVERT=1 in the game's
// environment turns every force the other way. Each returns 0 while no such
// controller is connected.
#define MH_FORCE_SLOTS 8
int MH_ForceAvailable(void);
// A constant force of `level` for `ms` milliseconds (0: no end)
int MH_ForceConstant(int slot, int level, int ms);
// A shake: a sine of `magnitude` 0..255 and `period_ms`, for `ms` (0: no end)
int MH_ForceSine(int slot, int magnitude, int period_ms, int ms);
// Stop the slot's effect (-1: every slot)
void MH_ForceStop(int slot);
void MH_ForceKeep(void);
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

// MIDI music, played outside the ARM: on an MT32-pi on the core's user port
// (host version 15), a MIDI synthesizer on a Raspberry Pi
// (github.com/dwhinham/mt32-pi: FluidSynth with SoundFonts, Munt for the
// MT-32), which MiSTer players connect for ao486, or on the SoundFont 2
// synthesizer in the core (host version 16, docs/midi_synth.md). The game
// sends MIDI messages, each at its audio frame as MH_FMWrite()'s; the library
// sends them to the Pi (through the FM ring, the core mixes its sound in) or
// works out the notes' voices for the synthesizer. Where the music goes is
// the player's, in the OSD: "O[87],Music,MIDI,FM chip;" (FM chip: the game
// plays its FM music), the MT32-pi's lines (status bits 96..104, shown only
// while one is connected; "MT32-pi: On" sends the music to it) and the
// synthesizer's "O[90:88],SoundFont,Default,1,2,3,4,5,6,7;", the per-game
// choice from MH_SoundFontDir(), which the library loads, and another when
// the OSD changes, by itself. The template's CONF_STR has them all for a core
// whose games play MIDI. With an older core MH_MidiAvailable() is 0 and the
// calls below do nothing; without a Pi or the synthesizer, or with the OSD on
// the FM chip, MH_MidiWanted() is 0: the game plays its FM music then.
int MH_MidiAvailable(void);
// Needs MH_AudioEnable(1) first. Enabling starts loading the synthesizer's
// SoundFont in a thread (the game goes on meanwhile; its notes come once it
// is there). Disabling stops the notes and forgets the channels;
// MH_AudioEnable(0) does too. A game enables it once and asks MH_MidiWanted()
// for where its music goes.
void MH_MidiEnable(int enabled);
// 1 while the game should play its MIDI music: MIDI is enabled, the OSD says
// MIDI and an MT32-pi is connected and on in the OSD, or the synthesizer has
// a SoundFont loaded or being loaded. 0 when there is no SoundFont that could
// be loaded (Main_MiSTer's info box says why): the game plays its FM music
// then. Ask it when a song starts, or every frame to follow the OSD. Where the
// music goes is logged.
int MH_MidiWanted(void);
// 1 once notes sound: with the MT32-pi, as MH_MidiWanted(); with the
// synthesizer, once the SoundFont is loaded (note-ons sent before are
// dropped), so a game may hold its first song until then
int MH_MidiReady(void);
// 1 once each time the synthesizer has lost the song's settings since the
// last call: the music went from the MT32-pi to the synthesizer in the core or
// back, or the MT32-pi switched synth, MT-32 ROM or SoundFont (it reports that
// when it has loaded it), was reset, or the OSD's "Reset Hanging Notes" was
// chosen. Its channels are then as after a reset (programs, volumes,
// controllers): while MH_MidiWanted(), the game sends the song's again. (The
// synthesizer in the core keeps its channels through a change of SoundFont.)
int MH_MidiSynthChanged(void);
// 0..127: 32 is the sound as it comes (the MT32-pi's, the synthesizer's),
// 127 nearly four times that
void MH_MidiVolume(int volume);
// A MIDI message (status byte 0x80..0xEF, its data bytes; the second is not
// sent for a program change or channel pressure), made at audio frame
// `ahead` of the block the game will queue next, as MH_FMWrite(). Never
// blocks; dropped when the ring is full.
void MH_MidiEvent(int ahead, int status, int data1, int data2);
// A system exclusive message, without its F0 and F7 (the synthesizer knows
// the GM, GS and XG resets and the GS drum parts)
void MH_MidiSysEx(int ahead, const uint8_t* data, int length);
// All sound and notes off, controllers reset, on all 16 channels
void MH_MidiReset(int ahead);
// Audio frames, counted as `ahead` is, until what was sent to the MT32-pi has
// gone out on its cable (31250 baud: 320us, about 14 frames, a byte); 0 while
// the music goes to the synthesizer, which takes it at its frame. A song that
// starts (or goes on after a seek, a pause, a switch of synthesizer) behind a
// burst of its own (MH_MidiReset() alone is 144 bytes, 46ms) holds its events
// back that long: otherwise its first notes go out late and their ends nearly
// on time, and are cut short (CrispyDoom, a player's report, 2026-10-23).
int MH_MidiBacklog(void);
// Where the synthesizer's SoundFonts are: games/dnk/SoundFonts on the first
// USB drive that has it, else on the SD card (MISTER_HYBRID_SOUNDFONTS in the
// environment for another folder)
const char* MH_SoundFontDir(void);

// Host version 17, for a program that is a MIDI player rather than a game
// (docs/midiplayer.md). Without them a core's music follows its OSD, as above.
//
// Where the music goes, the program's choice rather than the OSD's (the
// MT32-pi's line, the Music line): MH_MIDI_SYNTH the synthesizer in the core,
// MH_MIDI_PI an MT32-pi while one is connected (else the synthesizer),
// MH_MIDI_OSD the OSD's again. MH_MidiSynthChanged() is 1 after a switch, as
// after the OSD's.
enum { MH_MIDI_OSD, MH_MIDI_SYNTH, MH_MIDI_PI };
void MH_MidiDest(int dest);
// 1 while an MT32-pi is connected to the core's user port
int MH_MidiPiConnected(void);
// The synthesizer plays the SoundFont `file` (a name in MH_SoundFontDir(), or
// a path) instead of the OSD slot's, until MH_MidiLoadSoundFont(NULL) gives
// the choice back to the OSD. The load is the OSD's: in a thread, the music
// faded out meanwhile, the channels kept; MH_MidiReady() is 0 until it is
// there. No info box when it fails: MH_MidiSoundFont() says so. 0 when the
// core has no synthesizer.
int MH_MidiLoadSoundFont(const char* file);
// The synthesizer's SoundFont: MH_SF_LOADED (its path in `file`),
// MH_SF_LOADING (the one coming, when known), MH_SF_FAILED (the last one
// asked for is no SoundFont 2, or too big, or missing), MH_SF_NONE. `file` may
// be NULL.
enum { MH_SF_NONE, MH_SF_LOADING, MH_SF_LOADED, MH_SF_FAILED };
int MH_MidiSoundFont(char* file, int size);
// The SoundFont slots of every core's OSD line SoundFont (docs/midiplayer.md,
// "slots.txt"): for each of the 8 (0 the Default) the file of
// MH_SoundFontDir() it plays and whether it is its own (own[k] 0: an empty
// slot, which plays slot 0's). 0 when the folder has no font.
#define MH_SF_SLOTS 8
#define MH_SF_NAME 256
int MH_SoundFontSlots(char files[MH_SF_SLOTS][MH_SF_NAME], int own[MH_SF_SLOTS]);
// Puts a file of MH_SoundFontDir() in a slot, NULL or "" empties it: writes
// slots.txt with every slot as it is now, so no other one moves. A core reads
// it when its game loads a font. 0 when it cannot be written.
int MH_SetSoundFontSlot(int slot, const char* file);

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
// The game's folder as the player sees it ("games/dnk/ECWolf" on the SD card,
// "usb0/games/dnk/ECWolf" on a USB drive): the current one, for messages
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
// in it is the game's folder (MH_GamePath()). The game it returns gets its
// logo (MH_GameLogo(), its `name` the title; a shareware its full game's
// logo; MH_GameLogoLoading() for a game without files to check), as does one
// whose files are missing has "Checking Game Data..." before the message
// (MH_GameLogoMissing()).
int MH_PickGame(const mh_game* games, int count, const char* dir, const char* none, int* listed);

// The game's logo at its start (host version 17, docs/game_logo.md): once the
// core's intro is over, "Checking Game Data..." and "Game Data OK" under the
// MiSTer logo; it goes with them, "Loading <title>..." comes on the empty
// screen and the game's logo in the MiSTer logo's place (logos/<id>.mhl next
// to the program, made by package.sh with tools/make_logo.py). The game loads
// meanwhile; its first frame (MH_Present(), MH_PresentBuffer())
// waits until the logo has been there 2 seconds, and its palette and mode are
// taken with that frame. MH_PickGame() calls it with the game it returns: a
// port with pick code of its own calls it once it knows the game. Without the
// file, or with a core of host version 16 or older: "Loading <title>..." under
// the MiSTer logo, and 0. A program started in this one's place (execv after
// MH_Close()) takes the logo over in MH_Open().
int MH_GameLogo(const char* id, const char* title);
// A game without files to check (MH_PickGame() with none, the MIDI Player):
// the MiSTer logo goes once the intro is over, then "Loading <title>..." alone
// and its logo
int MH_GameLogoLoading(const char* id, const char* title);
// The overlay (host version 17): the program's frames, 640x480 at 8bpp (its
// mode and format are taken as that), are drawn over the core's picture
// instead of taking the screen; index 0 is transparent. The purple screen and
// its badge stay; the program keeps clear of the badge (x >= 520, y >= 380).
// Its first frame comes in as the overlay's handover has it (docs/
// midiplayer.md, "The screen"), and after the game's logo, once the logo has
// gone out. Called before the first frame; 0 with a core of host version 16
// or older, whose screen the frames then take as ever.
int MH_Overlay(int on);
// The game's files are not there: no logo, "Checking Game Data..." under the
// MiSTer logo once its intro is over, for 0.4 seconds, before the message
// naming them (MH_UI_Message() with MH_UI_WAIT) takes its place. MH_PickGame()
// does it for a game's menu entry.
void MH_GameLogoMissing(const char* id);

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
