// SPDX-License-Identifier: GPL-2.0-or-later
// Part of Hybrid_MiSTer (hps/LICENSE).
//
// Which game of a core with several (hybrid/README.md, "Several games in one
// core"): from the menu entry the core was started with, or a list of the
// games whose files are there.

#define _GNU_SOURCE
#include "mister_hybrid.h"

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
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
        // from where the game runs: games/dnk/<Engine> on the SD card,
        // usb0/games/dnk/<Engine> on a USB drive
        snprintf(path, sizeof(path), "%s",
            strncmp(cwd, "/media/fat/", 11) == 0 ? cwd + 11 : strncmp(cwd, "/media/", 7) == 0 ? cwd + 7 : cwd);
    }
    return path;
}

// The game's logo: a shareware's is its full game's (games.txt has no line
// for it)
static const char* logo_id(const mh_game* game) {
    return game->shareware_of != NULL ? game->shareware_of : game->id;
}

// The game picked, its logo and lines (host version 17)
static int picked(const mh_game* games, int pick) {
    if (MH_IsOpen() && (games[pick].files == NULL || games[pick].files[0] == NULL)) {
        MH_GameLogoLoading(logo_id(&games[pick]), games[pick].name);
    } else if (MH_IsOpen()) {
        MH_GameLogo(logo_id(&games[pick]), games[pick].name);
    }
    return pick;
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

// The game picked, for a reset (host version 18, MH_EXIT_RESET): the launch
// script starts the program again with MISTER_HYBRID_RESET=1, and the same
// game is picked, as it was (from the list: quitting it brings the list back)
#define PICK_FILE "/tmp/dnk_pick"

static int pick_note(const char* core, const mh_game* games, int pick, int listed) {
    FILE* f = fopen(PICK_FILE, "w");

    if (f != NULL) {
        fprintf(f, "%s %s %d\n", core, games[pick].id, listed);
        fclose(f);
    }
    return picked(games, pick);
}

static int pick_again(const char* core, const mh_game* games, int count, const char* dir, int* listed) {
    const char* env = getenv("MISTER_HYBRID_RESET");
    char was[64], id[64];
    int i, again = 0;
    FILE* f;

    if (env == NULL || strcmp(env, "1") != 0 || (f = fopen(PICK_FILE, "r")) == NULL) {
        return -1;
    }
    if (fscanf(f, "%63s %63s %d", was, id, &again) != 3 || strcmp(was, core) != 0) {
        fclose(f);
        return -1;
    }
    fclose(f);
    for (i = 0; i < count; i++) {
        if (strcasecmp(games[i].id, id) == 0 && MH_GameFound(&games[i], dir)) {
            *listed = again != 0;
            return pick_note(core, games, i, *listed);
        }
    }
    return -1;
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
    // after a reset: the game it was
    if ((pick = pick_again(core, games, count, dir, listed)) >= 0) {
        return pick;
    }
    // started from a game's menu entry: <core>_<id>
    if (len != 0 && strncmp(set, core, len) == 0 && set[len] == '_') {
        for (i = 0; i < count; i++) {
            if (games[i].shareware_of != NULL || strcasecmp(games[i].id, set + len + 1) != 0) {
                continue;
            }
            if (MH_GameFound(&games[i], dir)) {
                return pick_note(core, games, i, 0);
            }
            // the shareware or demo of it, when the full game is not there
            for (j = 0; j < count; j++) {
                if (games[j].shareware_of != NULL && strcasecmp(games[j].shareware_of, games[i].id) == 0
                    && MH_GameFound(&games[j], dir)) {
                    return pick_note(core, games, j, 0);
                }
            }
            if (MH_IsOpen()) {
                MH_GameLogoMissing(games[i].id);
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
        return pick_note(core, games, found[0], 0);
    }
    MH_GameList();
    pick = MH_UI_Menu(core, "Select a game:", names, n, 0);
    if (pick < 0 || pick >= n) {
        return -1;
    }
    *listed = 1;
    return pick_note(core, games, found[pick], 1);
}

// The game's files from a CD image (MH_UnpackDisc()): the data track's ISO
// 9660 file system, read straight from the image. A raw image (.bin, GOG's
// GAME.DAT) has sectors of 2352 bytes, the 2048 of data after a sync pattern
// and a header (16 bytes in mode 1, 24 in mode 2); an .iso has the 2048 alone.
// The data track is the image's first, as on every CD with music tracks.
#define DISC_FILES 1024

typedef struct disc {
    FILE* f;
    long sector;  // bytes a sector in the image
    long offset;  // of the data in one
    char name[256];
    uint64_t total; // bytes to unpack
} disc;

typedef struct disc_file {
    char path[256]; // where it goes, from the game's folder: "DATA/LEVEL1.PHD"
    uint32_t lba, size;
} disc_file;

static int disc_read(disc* d, uint32_t lba, uint8_t* buf, int count) {
    int i;

    for (i = 0; i < count; i++) {
        if (fseek(d->f, (long)(lba + i) * d->sector + d->offset, SEEK_SET) != 0
            || fread(buf + i * 2048, 2048, 1, d->f) != 1) {
            errno = EIO; // cut short: no error of its own
            return 0;
        }
    }
    return 1;
}

static uint32_t le32(const uint8_t* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

// 1 if `path` is a CD image with an ISO 9660 file system: its kind in `d`
static int disc_open(disc* d, const char* path) {
    static const uint8_t sync[12] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
    uint8_t head[16], pvd[2048];

    if ((d->f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    if (fread(head, sizeof(head), 1, d->f) == 1 && memcmp(head, sync, sizeof(sync)) == 0) {
        d->sector = 2352;
        d->offset = head[15] == 2 ? 24 : 16;
    } else {
        d->sector = 2048;
        d->offset = 0;
    }
    if (disc_read(d, 16, pvd, 1) && pvd[0] == 1 && memcmp(pvd + 1, "CD001", 5) == 0) {
        return 1;
    }
    fclose(d->f);
    d->f = NULL;
    return 0;
}

// The files of the directory at `lba` (`size` bytes) and of the ones in it,
// their paths under `path`
static int disc_list(disc* d, uint32_t lba, uint32_t size, const char* path, disc_file* files, int n, int depth) {
    uint8_t* buf;
    uint32_t pos;
    int sectors = (int)((size + 2047) / 2048);

    if (depth > 8 || sectors > 64 || (buf = malloc((size_t)sectors * 2048)) == NULL) {
        return n;
    }
    if (!disc_read(d, lba, buf, sectors)) {
        free(buf);
        return n;
    }
    for (pos = 0; pos < (uint32_t)sectors * 2048 && n < DISC_FILES;) {
        const uint8_t* r = buf + pos;
        char name[128], sub[256];
        int len;

        if (r[0] == 0) { // the records of a sector end: on to the next
            pos = (pos / 2048 + 1) * 2048;
            continue;
        }
        len = r[32];
        pos += r[0];
        if (len == 0 || r[33] <= 1 || 33 + len > r[0]) { // "." and ".."
            continue;
        }
        snprintf(name, sizeof(name), "%.*s", len, (const char*)r + 33);
        name[strcspn(name, ";")] = 0; // "LEVEL1.PHD;1"
        len = (int)strlen(name);
        if (len > 0 && name[len - 1] == '.') {
            name[len - 1] = 0;
        }
        snprintf(sub, sizeof(sub), "%s/%s", path, name);
        if (r[25] & 2) {
            n = disc_list(d, le32(r + 2), le32(r + 10), sub, files, n, depth + 1);
        } else {
            snprintf(files[n].path, sizeof(files[n].path), "%s", sub);
            files[n].lba = le32(r + 2);
            files[n].size = le32(r + 10);
            n++;
        }
    }
    free(buf);
    return n;
}

// `path` (from `dir`) with every part as it is already there in any case,
// the parts not there yet as they are
static void disc_local(const char* dir, const char* path, char* out, size_t size) {
    const char* p = path;

    snprintf(out, size, "%s", dir);
    while (*p != 0) {
        size_t n = strcspn(p, "/"), len = strlen(out);
        struct dirent* ent;
        DIR* d = opendir(out);
        int hit = 0;

        while (d != NULL && !hit && (ent = readdir(d)) != NULL) {
            if (strlen(ent->d_name) == n && strncasecmp(ent->d_name, p, n) == 0) {
                snprintf(out + len, size - len, "/%s", ent->d_name);
                hit = 1;
            }
        }
        if (d != NULL) {
            closedir(d);
        }
        if (!hit) {
            snprintf(out + len, size - len, "/%.*s", (int)n, p);
        }
        p += n;
        while (*p == '/') {
            p++;
        }
    }
}

// The line under "Checking Game Data...", when the percentage changes
static void disc_progress(const disc* d, uint64_t done, uint64_t total, int* shown) {
    char text[MH_MSG_ROWS * (MH_MSG_COLS + 1)];
    int pct = total != 0 ? (int)(done * 100 / total) : 100;

    if (pct != *shown && MH_IsOpen()) {
        *shown = pct;
        snprintf(text, sizeof(text), "Checking Game Data...\nUnpacking %.40s... %d%%", d->name, pct);
        MH_ShowMessage(text, 0);
    }
}

// One file of the image to `out`, through `out`.part
static int disc_copy(disc* d, const disc_file* file, const char* out, uint64_t* done, uint64_t total, int* shown) {
    static uint8_t buf[64 * 2048];
    char part[600];
    uint32_t left = file->size, lba = file->lba;
    FILE* f;

    snprintf(part, sizeof(part), "%s.part", out);
    if ((f = fopen(part, "wb")) == NULL) {
        return 0;
    }
    while (left > 0) {
        int sectors = (int)((left + 2047) / 2048);
        uint32_t n;

        sectors = sectors > 64 ? 64 : sectors;
        n = left < (uint32_t)sectors * 2048 ? left : (uint32_t)sectors * 2048;
        if (!disc_read(d, lba, buf, sectors) || fwrite(buf, n, 1, f) != 1) {
            fclose(f);
            unlink(part);
            return 0;
        }
        lba += sectors;
        left -= n;
        *done += n;
        disc_progress(d, *done, total, shown);
    }
    if (fclose(f) != 0 || rename(part, out) != 0) {
        unlink(part);
        return 0;
    }
    return 1;
}

static int disc_unpack(disc* d, const char* dir, const char* const* folders) {
    static disc_file files[DISC_FILES];
    uint8_t pvd[2048];
    uint64_t total = 0, done = 0;
    int n, i, j, copied = 0, shown = -1, want[DISC_FILES];
    char out[512];
    struct stat st;

    if (!disc_read(d, 16, pvd, 1)) {
        return 0;
    }
    // the folders asked for, of the root directory's
    n = disc_list(d, le32(pvd + 156 + 2), le32(pvd + 156 + 10), "", files, 0, 0);
    for (i = 0; i < n; i++) {
        want[i] = 0;
        for (j = 0; folders[j] != NULL; j++) {
            size_t len = strlen(folders[j]);
            if (strncasecmp(files[i].path + 1, folders[j], len) == 0 && files[i].path[1 + len] == '/') {
                want[i] = 1;
            }
        }
        // a file that is there already, as big as on the disc, stays
        disc_local(dir, files[i].path + 1, out, sizeof(out));
        if (want[i] && stat(out, &st) == 0 && st.st_size == (off_t)files[i].size) {
            want[i] = 0;
        }
        total += want[i] ? files[i].size : 0;
    }
    for (i = 0; i < n; i++) {
        if (want[i]) {
            break;
        }
    }
    if (i == n) {
        return 0;
    }
    d->total = total;
    printf("mister: unpacking %llu bytes from %s\n", (unsigned long long)total, d->name);
    MH_GameLogoMissing(NULL); // "Checking Game Data..." once the intro is over
    disc_progress(d, 0, total, &shown);
    for (i = 0; i < n; i++) {
        char* slash;

        if (!want[i]) {
            continue;
        }
        disc_local(dir, files[i].path + 1, out, sizeof(out));
        // its folders
        for (slash = strchr(out + strlen(dir) + 1, '/'); slash != NULL; slash = strchr(slash + 1, '/')) {
            *slash = 0;
            if (mkdir(out, 0777) != 0 && errno != EEXIST) {
                printf("mister: %s: %s\n", out, strerror(errno));
                return -1;
            }
            *slash = '/';
        }
        if (!disc_copy(d, &files[i], out, &done, total, &shown)) {
            printf("mister: %s: %s\n", out, strerror(errno));
            return -1;
        }
        copied++;
    }
    sync(); // on the card before the game goes on
    return copied;
}

int MH_UnpackDisc(const char* dir, const char* const* folders) {
    const char* core = getenv("MISTER_HYBRID_CORE");
    struct dirent* ent;
    DIR* dd;
    int result = 0;

    dir = dir != NULL ? dir : ".";
    if ((dd = opendir(dir)) == NULL) {
        return 0;
    }
    while (result == 0 && (ent = readdir(dd)) != NULL) {
        char path[512];
        struct stat st;
        disc d;

        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        // big enough for a file system's descriptor, and no folder
        if (ent->d_name[0] == '.' || stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 17 * 2352
            || !disc_open(&d, path)) {
            continue;
        }
        snprintf(d.name, sizeof(d.name), "%s", ent->d_name);
        result = disc_unpack(&d, dir, folders);
        if (result < 0) {
            char text[MH_MSG_ROWS * (MH_MSG_COLS + 1)], names[128] = "";
            int err = errno, i;

            // "DATA and FMV"
            for (i = 0; folders[i] != NULL; i++) {
                size_t len = strlen(names);
                snprintf(names + len, sizeof(names) - len, "%s%s", i == 0 ? "" : folders[i + 1] == NULL ? " and " : ", ",
                    folders[i]);
            }
            snprintf(text, sizeof(text), "The game's files could not be unpacked from %.40s (%s). They need %d MB "
                "in %s/. Or copy the %s folders there from the image yourself.", d.name, strerror(err),
                (int)((d.total + 999999) / 1000000), MH_GamePath(), names);
            MH_UI_Message(core ? core : "", text, MH_UI_WAIT | MH_UI_ERROR);
        }
        fclose(d.f);
    }
    closedir(dd);
    return result;
}
