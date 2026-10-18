// MiSTer hybrid core support, see mister_port.h

#include "mister_port.h"

#include <mister_hybrid.h>
#include <stdio.h>

#define EXIT_TO_MENU 0
#define EXIT_RESTART 42 // danik_hybrid_launch.sh starts the game again
#define EXIT_RELOADED 43 // the same, after the core has settled

static const char *const TITLE = "@NAME@";

// OSD options of the game (CONF_STR in core/@NAME@.sv, status bits 24..63).
// The first entry of each is 0, the default. For example, for
//   "O[25:24],Difficulty,Normal,Easy,Hard;"
// int MiSTer_Difficulty(void) { return MH_OSD_GAME_BITS(MH_OSDStatus(), 24, 2); }

// GAME: 1 if the game's frames take most of a field: each is drawn a field
// ahead and the library shows it at its own field (MH_PresentAtField()), so
// that one a little slower than a field is still in time, for one field more
// latency. Decide by benchmark, hybrid/CLAUDE.md ("Frame pacing").
#define DRAW_AHEAD 0

static uint32_t LastField; // with DRAW_AHEAD the field the last frame is shown at
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
    if (DRAW_AHEAD && !MH_Interlaced()) {
        uint32_t target = LastField + 1;

        MH_WaitField(target - 3); // not before the field two before its own
        field = MH_FieldCounter();
        if ((int32_t)(field + 1 - target) > 0)
            target = field + 1; // too late for it: the next vblank
        if (!MH_IsOpen())
            return 0;
        MH_PresentAtField(target);
        fields = target - LastField;
        LastField = target;
        return fields;
    }
    MH_WaitFrame(LastField);
    field = MH_FieldCounter();
    fields = field - LastField; // 0: the core is gone
    LastField = field;
    return fields;
}

void MiSTer_HudRows(int height, int *top, int *bottom)
{
    int scale = height >= 480 ? 2 : 1;
    int t, b;

    MH_HudMargins(&t, &b);
    *top = t * scale;
    *bottom = height - b * scale;
}

// The player came through the list: quitting a game goes back to it
static int PickedFromList = 0;

// GAME: what is said when none of the games is there (4 lines of 71; %s: the
// game's folder, MH_GamePath())
// GAME: the message when no game's files are there: name them (by name or
// pattern) and where they go, %s (hybrid/CLAUDE.md, "missing game files")
#define NO_GAME "No game data found. Copy TODO (the files, by name) to %s/."

int MiSTer_PickGame(const struct mh_game *games, int count)
{
    char text[MH_MSG_COLS + 1];
    int pick;

    if (!MH_Open())
        return MH_PickGame(games, count, NULL, NO_GAME, &PickedFromList);
    pick = MH_PickGame(games, count, NULL, NO_GAME, &PickedFromList);
    if (pick >= 0) {
        snprintf(text, sizeof(text), "Loading %s...", games[pick].name);
        MH_UI_Message(TITLE, text, 0);
    }
    return pick;
}

void MiSTer_ShowError(const char *message)
{
    if (MH_Open())
        MH_UI_Message(TITLE, message, MH_UI_WAIT | MH_UI_ERROR);
}

int MiSTer_ExitCode(void)
{
    // The player loaded the core again while the game ran
    if (MH_CoreReloaded())
        return EXIT_RELOADED;
    // Not if we are quitting because another core was loaded
    if (PickedFromList && MH_Open())
        return EXIT_RESTART;
    return EXIT_TO_MENU;
}
