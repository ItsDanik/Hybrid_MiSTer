// MiSTer hybrid core support, see mister_port.h

#include "mister_port.h"

#include <mister_hybrid.h>

#define EXIT_TO_MENU 0
#define EXIT_RESTART 42 // danik_hybrid_launch.sh starts the game again

static const char *const TITLE = "@NAME@";

// OSD options of the game (CONF_STR in core/@NAME@.sv, status bits 24..63).
// The first entry of each is 0, the default. For example, for
//   "O[25:24],Difficulty,Normal,Easy,Hard;"
// int MiSTer_Difficulty(void) { return MH_OSD_GAME_BITS(MH_OSDStatus(), 24, 2); }

static uint32_t LastField;
static int FieldValid = 0;

void MiSTer_ResetFrames(void)
{
    FieldValid = 0;
}

unsigned MiSTer_WaitFrame(void)
{
    uint32_t field;
    unsigned fields;

    if (!MH_IsOpen())
        return 0;
    if (!FieldValid) {
        LastField = MH_FieldCounter();
        FieldValid = 1;
    }
    MH_WaitField(LastField);
    field = MH_FieldCounter();
    fields = field - LastField; // 0: the core is gone
    LastField = field;
    return fields;
}

// The player came through the list: quitting a game goes back to it
static int PickedFromList = 0;

int MiSTer_PickGame(const char *const *names, int count, int selected)
{
    int pick;

    if (!MH_Open())
        return selected;
    pick = MH_UI_Menu(TITLE, "Select a game:", names, count, selected);
    if (pick < 0 || pick >= count)
        return -1;
    PickedFromList = 1;
    MH_UI_Message(TITLE, "Loading...", 0);
    return pick;
}

void MiSTer_ShowError(const char *message)
{
    if (MH_Open())
        MH_UI_Message(TITLE, message, MH_UI_WAIT | MH_UI_ERROR);
}

int MiSTer_ExitCode(void)
{
    // Not if we are quitting because another core was loaded
    if (PickedFromList && MH_Open())
        return EXIT_RESTART;
    return EXIT_TO_MENU;
}
