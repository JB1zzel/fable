/* main_win.c - Windows entry point for Fable. */

#include <windows.h>
#include <stdio.h>
#include "file_win.h"

extern int fable_main(int argc, char **argv);

int main(int argc, char **argv)
{
    fab_mkdir("/diary");          /* the calendar (F6) saves here */
    {
        char cwd[MAX_PATH];
        FILE *log = fopen("fable.log", "w");
        if (log) {
            GetCurrentDirectoryA(sizeof cwd, cwd);
            fprintf(log, "cwd: %s\n", cwd);
            fprintf(log, "dict.smd exists (Win32): %s\n",
                    GetFileAttributesA("dict.smd") != INVALID_FILE_ATTRIBUTES
                        ? "YES" : "NO");
            fprintf(log, "fab_exists(\"dict.smd\") = %d\n", fab_exists("dict.smd"));
            fprintf(log, "fab_exists(\"/dict.smd\") = %d\n", fab_exists("/dict.smd"));
            fprintf(log, "fab_exists(\"/\") = %d\n", fab_exists("/"));
            {
                FabDirEntry e[8];
                int n = fab_dir_entries("/", e, 8, NULL);
                int i;
                fprintf(log, "fab_dir_entries(\"/\") returned %d entries\n", n);
                for (i = 0; i < n; i++)
                    fprintf(log, "  %s%s\n", e[i].name, e[i].is_dir ? "/" : "");
            }
            fclose(log);
        }
    }
    return fable_main(argc, argv);
}