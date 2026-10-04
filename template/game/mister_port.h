#ifndef MISTER_PORT_H
#define MISTER_PORT_H

// MiSTer hybrid core: the game runs on the ARM, the FPGA core shows the
// picture at 15kHz and provides audio and input. SDL's "mister" drivers
// (hybrid/sdl2) do most of the work; this is what is specific to the game.
//
// Starting point for a port: copy both files into the game's source, build
// them when MISTER_HYBRID is on, and call them from the few places named
// below. Everything is a no-op without the core, so the PC build still runs.

#ifdef __cplusplus
extern "C" {
#endif

// Joystick buttons as SDL sees them: the buttons of the core's "J1," list
// (core/<Name>.sv) in order, then the two the SDL driver adds. Rename the
// first eight after the game's actions.
enum {
    MISTER_JOY_ACTION1,
    MISTER_JOY_ACTION2,
    MISTER_JOY_ACTION3,
    MISTER_JOY_ACTION4,
    MISTER_JOY_ACTION5,
    MISTER_JOY_ACTION6,
    MISTER_JOY_ACTION7,
    MISTER_JOY_MENU,
    MISTER_JOY_CORE_OK,
    MISTER_JOY_CORE_BACK,

    MISTER_JOY_MENU_OK = 28,
    MISTER_JOY_MENU_BACK = 29
};
// In the game's menus: confirm and go back
#define MISTER_JOY_OK_MASK ((1 << MISTER_JOY_CORE_OK) | (1 << MISTER_JOY_MENU_OK))
#define MISTER_JOY_BACK_MASK ((1 << MISTER_JOY_CORE_BACK) | (1 << MISTER_JOY_MENU_BACK))

// Frame pacing: call once per frame in place of the game's own wait. Sleeps
// until the core's next field and returns how many fields (1/59.64s) have
// passed since the last call, which is the game's clock. 0 without the core:
// use the original timing then.
unsigned MiSTer_WaitFrame(void);
// The next call starts counting anew, after the game stood still (loading)
void MiSTer_ResetFrames(void);

// Before the game starts: lets the player pick one of several data sets on
// the screen. Returns the index, -1 to quit
int MiSTer_PickGame(const char *const *names, int count, int selected);
// Problems the player can fix (missing data): shown on the screen until a
// button is pressed, in plain words that say which files go where
void MiSTer_ShowError(const char *message);
// Exit code of the process after the player quit: the launcher starts the
// game again (back to the list of MiSTer_PickGame) or returns to the menu
int MiSTer_ExitCode(void);

#ifdef __cplusplus
}
#endif

#endif
