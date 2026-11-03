/* file_win.h - Windows equivalent of file_fabgl.h (no stdio redirection). */

#ifndef FILE_WIN_H
#define FILE_WIN_H

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     name[64];
    uint32_t size;
    uint32_t mtime;
    int      is_dir;
} FabDirEntry;

int fab_exists(const char *path);
int fab_remove(const char *path);
int fab_rename(const char *from, const char *to);
int fab_dir_entries(const char *path, FabDirEntry *out, int max);
uint64_t fab_card_total(void);
uint64_t fab_card_used(void);

#ifdef __cplusplus
}
#endif

#endif