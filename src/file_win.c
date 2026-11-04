/* file_win.c - Windows implementation of the fab_* file helpers.
 *
 * Paths are translated so that a "/"-style path (as used on the ESP32
 * SD card) maps to the current working directory on Windows:
 *
 *   "/"         -> current directory
 *   "/dict.smd" -> dict.smd in the current directory
 *   "/foo/bar"  -> foo\bar
 *   "foo/bar"   -> foo\bar
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "file_win.h"

/* ---- path translation ---- */

static void make_win_path(const char *path, char *out, int out_len)
{
    int i = 0, j = 0;

    if (!path || path[0] == '\0' ||
        (path[0] == '/' && path[1] == '\0')) {
        out[0] = '\0';
        return;
    }

    while (path[i] == '/') i++;
    while (path[i] && j < out_len - 1) {
        out[j++] = (path[i] == '/') ? '\\' : path[i];
        i++;
    }
    out[j] = '\0';
}

/* ---- existence, remove, rename ---- */

int fab_exists(const char *path)
{
    char win[MAX_PATH];
    make_win_path(path, win, sizeof win);
    if (win[0] == '\0') return 1;      /* current dir always exists */
    return GetFileAttributesA(win) != INVALID_FILE_ATTRIBUTES;
}

int fab_remove(const char *path)
{
    char win[MAX_PATH];
    make_win_path(path, win, sizeof win);
    if (win[0] == '\0') return -1;
    return DeleteFileA(win) ? 0 : -1;
}

int fab_rename(const char *from, const char *to)
{
    char wfrom[MAX_PATH], wto[MAX_PATH];
    make_win_path(from, wfrom, sizeof wfrom);
    make_win_path(to, wto, sizeof wto);
    if (wfrom[0] == '\0' || wto[0] == '\0') return -1;
    return MoveFileA(wfrom, wto) ? 0 : -1;
}

/* ---- directory listing ---- */

static int name_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (unsigned char)*a++;
        int cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int cmp_entry(const void *pa, const void *pb)
{
    const FabDirEntry *a = (const FabDirEntry *)pa;
    const FabDirEntry *b = (const FabDirEntry *)pb;

    if (a->is_dir && !b->is_dir) return -1;
    if (!a->is_dir && b->is_dir) return  1;
    if (a->is_dir) return name_cmp(a->name, b->name);

    if (a->mtime > b->mtime) return -1;
    if (a->mtime < b->mtime) return  1;
    return name_cmp(a->name, b->name);
}

int fab_dir_entries(const char *path, FabDirEntry *out, int max)
{
    char win[MAX_PATH];
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0;

    if (!out || max <= 0) return 0;

    make_win_path(path, win, sizeof win);

    if (win[0] == '\0')
        snprintf(pattern, sizeof pattern, "*");
    else
        snprintf(pattern, sizeof pattern, "%s\\*", win);

    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        const char *nm = fd.cFileName;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;

        strncpy(out[n].name, nm, sizeof(out[n].name) - 1);
        out[n].name[sizeof(out[n].name) - 1] = '\0';
        out[n].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        out[n].size   = out[n].is_dir ? 0
                        : (uint32_t)(((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);

        {
            ULARGE_INTEGER t;
            t.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
            t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
            out[n].mtime = (uint32_t)(t.QuadPart / 10000000ULL - 11644473600ULL);
        }
        n++;
    } while (n < max && FindNextFileA(h, &fd));

    FindClose(h);

    if (n > 1) qsort(out, (size_t)n, sizeof(FabDirEntry), cmp_entry);
    return n;
}

/* ---- disk usage ---- */

uint64_t fab_card_total(void)
{
    ULARGE_INTEGER free_bytes, total_bytes;
    if (GetDiskFreeSpaceExA(".", &free_bytes, &total_bytes, NULL))
        return (uint64_t)total_bytes.QuadPart;
    return 0;
}

uint64_t fab_card_used(void)
{
    ULARGE_INTEGER free_bytes, total_bytes;
    if (GetDiskFreeSpaceExA(".", &free_bytes, &total_bytes, NULL))
        return (uint64_t)(total_bytes.QuadPart - free_bytes.QuadPart);
    return 0;
}