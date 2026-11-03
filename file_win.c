/* file_win.c - Windows implementation of the fab_* file helpers. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "file_win.h"

int fab_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

int fab_remove(const char *path)
{
    return DeleteFileA(path) ? 0 : -1;
}

int fab_rename(const char *from, const char *to)
{
    return MoveFileA(from, to) ? 0 : -1;
}

/* case-insensitive name compare */
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
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0;

    if (!out || max <= 0) return 0;

    if (path[0] == '\0') {
        snprintf(pattern, sizeof pattern, "*");
    } else {
        size_t len = strlen(path);
        if (len > 0 && (path[len-1] == '/' || path[len-1] == '\\'))
            snprintf(pattern, sizeof pattern, "%s*", path);
        else
            snprintf(pattern, sizeof pattern, "%s\\*", path);
    }

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
            /* convert FILETIME to unix epoch seconds */
            ULARGE_INTEGER t;
            t.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
            t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
            /* FILETIME is 100ns ticks since 1601-01-01; subtract 11644473600s */
            out[n].mtime = (uint32_t)(t.QuadPart / 10000000ULL - 11644473600ULL);
        }
        n++;
    } while (n < max && FindNextFileA(h, &fd));

    FindClose(h);

    if (n > 1) qsort(out, (size_t)n, sizeof(FabDirEntry), cmp_entry);
    return n;
}

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