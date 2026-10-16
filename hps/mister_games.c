// Which game of a core with several (hybrid/README.md, "Several games in one
// core"): from the menu entry the core was started with, or a list of the
// games whose files are there.

#define _GNU_SOURCE
#include "mister_hybrid.h"

#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

// 1 if `pattern`, a path in `dir` whose parts may have * and ? and be in any
// case, names something there
static int exists(const char* dir, const char* pattern) {
    char path[512], part[256];
    const char* p = pattern;

    snprintf(path, sizeof(path), "%s", dir);
    while (*p != 0) {
        size_t n = strcspn(p, "/");
        size_t len = strlen(path);
        struct dirent* ent;
        DIR* d;
        int hit = 0;

        snprintf(part, sizeof(part), "%.*s", (int)n, p);
        d = opendir(path);
        if (d == NULL) {
            return 0;
        }
        while (!hit && (ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0
                && fnmatch(part, ent->d_name, FNM_CASEFOLD) == 0) {
                snprintf(path + len, sizeof(path) - len, "/%s", ent->d_name);
                hit = 1;
            }
        }
        closedir(d);
        if (!hit) {
            return 0;
        }
        p += n;
        while (*p == '/') {
            p++;
        }
    }
    return 1;
}

const char* MH_GamePath(void) {
    static char path[256];
    char cwd[256];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        snprintf(path, sizeof(path), "the game's folder");
    } else {
        snprintf(path, sizeof(path), "%s", strncmp(cwd, "/media/fat/", 11) == 0 ? cwd + 11 : cwd);
    }
    return path;
}

// A message with the game's folder for its %s
static void message(const char* core, const char* text) {
    char buf[MH_MSG_ROWS * (MH_MSG_COLS + 1) + 64];

    snprintf(buf, sizeof(buf), text, MH_GamePath());
    MH_UI_Message(core, buf, MH_UI_WAIT | MH_UI_ERROR);
}

const char* const MH_NOT_THERE[] = { "", NULL };

int MH_GameFound(const mh_game* game, const char* dir) {
    int i;

    if (game->files == MH_NOT_THERE) {
        return 0;
    }
    for (i = 0; game->files != NULL && game->files[i] != NULL; i++) {
        if (!exists(dir ? dir : ".", game->files[i])) {
            return 0;
        }
    }
    return 1;
}

int MH_PickGame(const mh_game* games, int count, const char* dir, const char* none, int* listed) {
    const char* core = getenv("MISTER_HYBRID_CORE");
    const char* set = MH_SetName();
    const char* names[64];
    int found[64];
    size_t len;
    int n = 0, i, j, pick;

    *listed = 0;
    core = core ? core : "";
    len = strlen(core);
    // started from a game's menu entry: <core>_<id>
    if (len != 0 && strncmp(set, core, len) == 0 && set[len] == '_') {
        for (i = 0; i < count; i++) {
            if (games[i].shareware_of != NULL || strcasecmp(games[i].id, set + len + 1) != 0) {
                continue;
            }
            if (MH_GameFound(&games[i], dir)) {
                return i;
            }
            // the shareware or demo of it, when the full game is not there
            for (j = 0; j < count; j++) {
                if (games[j].shareware_of != NULL && strcasecmp(games[j].shareware_of, games[i].id) == 0
                    && MH_GameFound(&games[j], dir)) {
                    return j;
                }
            }
            message(core, games[i].missing);
            return -1;
        }
        // a set name this core does not know: as from the core
    }
    // started from the core itself: the games that are there
    for (i = 0; i < count && n < 64; i++) {
        if (MH_GameFound(&games[i], dir)) {
            names[n] = games[i].name;
            found[n++] = i;
        }
    }
    if (n == 0) {
        message(core, none);
        return -1;
    }
    if (n == 1 || !MH_IsOpen()) {
        return found[0];
    }
    pick = MH_UI_Menu(core, "Select a game:", names, n, 0);
    if (pick < 0 || pick >= n) {
        return -1;
    }
    *listed = 1;
    return found[pick];
}
