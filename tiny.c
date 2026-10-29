/* tiny.c - tiny text editor for TCC / Windows CMD, built on tui.h
 * compile: tcc tiny.c tui_console.c -luser32 -o tiny.exe
 *
 * Keys:
 *   Arrows          move cursor
 *   Shift+Left/Right word left / right
 *   Home / End      line start / end
 *   PgUp / PgDn     page up / down
 *   Enter           split line
 *   Backspace       delete char / merge up at col 0
 *   Delete          delete char / merge down at end
 *   F2              join with next line
 *   F3              save (prompts for name if untitled)
 *   F4              save, spellcheck, reload
 *   Ctrl+B          mark block; when you end the mark it is copied to clipboard
 *   Ctrl+X          mark, cut (delete) the block, clipboard keeps it
 *   Ctrl+P          paste clipboard at cursor
 *   Ctrl+F          find text (prompts on the status bar)
 *   Ctrl+N          new document
 *   Esc             cancel mark / prompt (if any), else quit (prompt if modified)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tui.h"

#define MAXLINES 4096
#define MAXLINE  1024
#define SCRW     80
#define SCRH     24

#define IS_WORD(c) (isalnum((unsigned char)(c)) || (c) == '_')

static char  lines[MAXLINES][MAXLINE];
static int   nlines;
static int   cur_line, cur_col;
static int   top_line  = 0;
static int   left_col  = 0;
static int   modified;
static char  filename[260];
static char  dict_path[260] = "dict.smd";
static char  tmp_path[264];

static void insert_char(int c);
static void split_line(void);

/* ---------- mark + clipboard ---------- */

typedef struct { int sl, sc, el, ec; } Mark;

static Mark mark;
static int  mark_active = 0;
static int  mark_exists = 0;

static void clear_mark(void)
{
    mark_active = 0;
    mark_exists = 0;
}

static void mark_range(int *l1, int *l2, int *c1, int *c2)
{
    if (mark.sl < mark.el) {
        *l1 = mark.sl; *l2 = mark.el;
        *c1 = mark.sc; *c2 = mark.ec;
    } else if (mark.sl > mark.el) {
        *l1 = mark.el; *l2 = mark.sl;
        *c1 = mark.ec; *c2 = mark.sc;
    } else {
        *l1 = *l2 = mark.sl;
        if (mark.sc < mark.ec) { *c1 = mark.sc; *c2 = mark.ec; }
        else                   { *c1 = mark.ec; *c2 = mark.sc; }
    }
}

static int in_mark(int line, int col)
{
    int l1, l2, c1, c2;
    if (!mark_exists) return 0;
    mark_range(&l1, &l2, &c1, &c2);
    if (line < l1 || line > l2) return 0;
    if (l1 == l2) return col >= c1 && col < c2;
    if (line == l1) return col >= c1;
    if (line == l2) return col < c2;
    return 1;
}

static char *clip = NULL;
static long  clip_len = 0;
static long  clip_cap = 0;

static void clip_clear(void) { clip_len = 0; }

static int clip_append(const char *s, long n)
{
    if (clip_len + n > clip_cap) {
        long new_cap = clip_cap ? clip_cap : 256;
        char *new_buf;
        while (new_cap < clip_len + n) new_cap *= 2;
        new_buf = (char *)realloc(clip, (size_t)new_cap);
        if (!new_buf) return 0;
        clip = new_buf;
        clip_cap = new_cap;
    }
    memcpy(clip + clip_len, s, (size_t)n);
    clip_len += n;
    return 1;
}

static void copy_mark_to_clip(void)
{
    int l1, l2, c1, c2, l;
    clip_clear();
    if (!mark_exists) return;
    mark_range(&l1, &l2, &c1, &c2);
    for (l = l1; l <= l2; l++) {
        int start = (l == l1) ? c1 : 0;
        int end   = (l == l2) ? c2 : (int)strlen(lines[l]);
        int len   = end - start;
        if (len < 0) len = 0;
        if (len > 0) clip_append(&lines[l][start], (long)len);
        if (l < l2)  clip_append("\n", 1);
    }
}

static void delete_mark(void)
{
    int l1, l2, c1, c2, i, len;

    if (!mark_exists) return;
    mark_range(&l1, &l2, &c1, &c2);

    if (l1 == l2) {
        len = (int)strlen(lines[l1]);
        if (c2 > len) c2 = len;
        if (c1 > c2) c1 = c2;
        memmove(&lines[l1][c1],
                &lines[l1][c2],
                (size_t)(len - c2 + 1));
        cur_line = l1;
        cur_col  = c1;
    } else {
        int first_len = (int)strlen(lines[l1]);
        int last_len  = (int)strlen(lines[l2]);
        int keep_first, keep_last, after;

        if (c1 > first_len) c1 = first_len;
        if (c2 > last_len)  c2 = last_len;
        keep_first = c1;
        keep_last  = last_len - c2;

        if (keep_first + keep_last + 1 <= MAXLINE) {
            memmove(&lines[l1][keep_first],
                    &lines[l2][c2],
                    (size_t)(keep_last + 1));
        } else {
            lines[l1][keep_first] = '\0';
        }

        after = nlines - l2 - 1;
        for (i = 0; i < after; i++) {
            size_t ln = strlen(lines[l2 + 1 + i]) + 1;
            memcpy(lines[l1 + 1 + i], lines[l2 + 1 + i], ln);
        }
        nlines -= (l2 - l1);

        cur_line = l1;
        cur_col  = keep_first;
    }
    modified = 1;
}

static void paste_clip(void)
{
    long i;
    for (i = 0; i < clip_len; i++) {
        if (clip[i] == '\n') split_line();
        else                  insert_char((unsigned char)clip[i]);
    }
    modified = 1;
}

/* ---------- find ---------- */

static int find_next(const char *needle)
{
    int nlen = (int)strlen(needle);
    int start_line = cur_line;
    int start_col  = cur_col + 1;
    int i;

    if (nlen == 0) return 0;

    for (i = 0; i < nlines; i++) {
        int ln = (start_line + i) % nlines;
        const char *s = lines[ln];
        int slen = (int)strlen(s);
        int from = (i == 0) ? start_col : 0;
        int k;

        if (from > slen) from = slen;
        for (k = from; k + nlen <= slen; k++) {
            if (memcmp(&s[k], needle, (size_t)nlen) == 0) {
                cur_line = ln;
                cur_col  = k;
                return 1;
            }
        }
    }
    return 0;
}

/* ---------- file io ---------- */

static void load_file(const char *fn)
{
    FILE *f = fopen(fn, "rb");
    int c;
    if (!f) return;
    nlines = 0;
    for (;;) {
        int len = 0;
        c = fgetc(f);
        while (c != EOF && c != '\n' && c != '\r') {
            if (len < MAXLINE - 1) lines[nlines][len++] = (char)c;
            c = fgetc(f);
        }
        lines[nlines][len] = '\0';
        nlines++;
        if (c == EOF) break;
        if (c == '\r') {
            int c2 = fgetc(f);
            if (c2 != '\n' && c2 != EOF) ungetc(c2, f);
        }
        if (nlines >= MAXLINES) break;
    }
    fclose(f);
    if (nlines == 0) { lines[0][0] = '\0'; nlines = 1; }
    modified = 0;
    top_line = 0;
    left_col = 0;
}

static int save_file(void)
{
    FILE *f = fopen(filename, "wb");
    int i;
    if (!f) return 0;
    for (i = 0; i < nlines; i++) {
        fputs(lines[i], f);
        if (i < nlines - 1) fputs("\r\n", f);
    }
    fclose(f);
    modified = 0;
    return 1;
}

/* ---------- word count ---------- */

static int count_words(void)
{
    int n = 0, i, k;
    for (i = 0; i < nlines; i++) {
        int in = 0;
        for (k = 0; lines[i][k]; k++) {
            if (IS_WORD(lines[i][k])) { if (!in) { n++; in = 1; } }
            else in = 0;
        }
    }
    return n;
}

/* ---------- status bar and prompt bar ---------- */

static void status_msg(const char *msg)
{
    char buf[SCRW + 1];
    int i;
    snprintf(buf, sizeof buf, " %s", msg);
    for (i = (int)strlen(buf); i < SCRW; i++) buf[i] = ' ';
    buf[SCRW] = '\0';
    tui_gotoxy(1, SCRH + 1);
    tui_color(TUI_BLACK, TUI_LGRAY);
    tui_puts(buf);
    tui_color(TUI_LGRAY, TUI_BLACK);
}

static void prompt_bar(const char *text)
{
    char buf[SCRW + 1];
    int i;
    snprintf(buf, sizeof buf, " %s", text);
    for (i = (int)strlen(buf); i < SCRW; i++) buf[i] = ' ';
    buf[SCRW] = '\0';
    tui_gotoxy(1, SCRH + 1);
    tui_color(TUI_WHITE, TUI_BLUE);
    tui_puts(buf);
    tui_gotoxy((int)strlen(text) + 2, SCRH + 1);
}

static void put_status(void)
{
    char left[SCRW + 1];
    char right[SCRW + 1];
    char shown[64];
    int w = count_words();
    int len, rlen;
    const char *hint;
    const char *base;
    const char *dot;
    int n;

    base = filename[0] ? filename : "(unnamed)";
    {
        const char *s = base;
        while (*s) {
            if (*s == '\\' || *s == '/') base = s + 1;
            s++;
        }
    }
    dot = strrchr(base, '.');
    if (dot && dot != base) n = (int)(dot - base);
    else                     n = (int)strlen(base);
    if (n > (int)sizeof(shown) - 1) n = (int)sizeof(shown) - 1;
    memcpy(shown, base, (size_t)n);
    shown[n] = '\0';

    if (mark_active) {
        hint = "^B END MARK  ESC CANCEL ";
    } else if (mark_exists) {
        hint = "^P PASTE  ^X CUT  ESC CLEAR ";
    } else {
        hint = "F3 SAVE  F4 SPELL  ^F FIND  ^N NEW  ESC EXIT ";
    }

    snprintf(left, sizeof left, " %s  %d:%d%s  w:%d%s",
             shown,
             cur_line + 1, cur_col + 1,
             left_col > 0 ? "+" : "",
             w,
             modified ? "  !!" : "");
    left[SCRW] = '\0';

    snprintf(right, sizeof right, "%s", hint);
    rlen = (int)strlen(right);

    len = (int)strlen(left);
    if (len + rlen > SCRW) {
        len = SCRW - rlen - 1;
        if (len < 0) len = 0;
        left[len] = '\0';
    }

    tui_gotoxy(1, SCRH + 1);
    tui_color(TUI_BLACK, TUI_LGRAY);
    {
        int i, gap = SCRW - rlen - len;
        tui_puts(left);
        for (i = 0; i < gap; i++) tui_putc(' ');
        tui_puts(right);
    }
    tui_color(TUI_LGRAY, TUI_BLACK);
}

/* ---------- render ---------- */

static void render(void)
{
    int i;

    if (cur_line < top_line) top_line = cur_line;
    if (cur_line >= top_line + SCRH) top_line = cur_line - SCRH + 1;
    if (top_line < 0) top_line = 0;
    if (top_line > nlines - 1) top_line = nlines - 1;

    if (cur_col < left_col) left_col = cur_col;
    if (cur_col >= left_col + SCRW) left_col = cur_col - SCRW + 1;
    if (left_col < 0) left_col = 0;

    tui_cursor(0, 0);

    for (i = 0; i < SCRH; i++) {
        int ln = top_line + i;
        tui_gotoxy(1, i + 1);
        if (ln < nlines) {
            int len = (int)strlen(lines[ln]);
            int shown = 0;
            if (left_col < len) {
                int avail = len - left_col;
                int lim = avail > SCRW ? SCRW : avail;
                int k;
                int prev_color = 0;
                for (k = 0; k < lim; k++) {
                    int src_col = left_col + k;
                    int col_id = in_mark(ln, src_col) ? 1 : 0;
                    if (col_id != prev_color) {
                        if (col_id == 1) tui_color(TUI_BLACK, TUI_LCYAN);
                        else             tui_color(TUI_LGRAY, TUI_BLACK);
                        prev_color = col_id;
                    }
                    tui_putc(lines[ln][src_col]);
                }
                if (prev_color != 0) tui_color(TUI_LGRAY, TUI_BLACK);
                shown = lim;
            }
            while (shown < SCRW) { tui_putc(' '); shown++; }
        } else {
            int k;
            for (k = 0; k < SCRW; k++) tui_putc(' ');
        }
    }
    put_status();

    tui_gotoxy(cur_col - left_col + 1, cur_line - top_line + 1);
    tui_cursor(1, 1);
}

/* ---------- cursor helpers ---------- */

static void clamp_col(void)
{
    int len = (int)strlen(lines[cur_line]);
    if (cur_col > len) cur_col = len;
    if (cur_col < 0) cur_col = 0;
}

static void word_left(void)
{
    if (cur_col == 0) {
        if (cur_line > 0) {
            cur_line--;
            cur_col = (int)strlen(lines[cur_line]);
        }
        return;
    }
    while (cur_col > 0 && !IS_WORD(lines[cur_line][cur_col - 1])) cur_col--;
    while (cur_col > 0 &&  IS_WORD(lines[cur_line][cur_col - 1])) cur_col--;
}

static void word_right(void)
{
    int len = (int)strlen(lines[cur_line]);
    if (cur_col >= len) {
        if (cur_line < nlines - 1) { cur_line++; cur_col = 0; }
        return;
    }
    while (cur_col < len &&  IS_WORD(lines[cur_line][cur_col])) cur_col++;
    while (cur_col < len && !IS_WORD(lines[cur_line][cur_col])) cur_col++;
}

/* ---------- edits ---------- */

static void insert_char(int c)
{
    int len = (int)strlen(lines[cur_line]);
    if (cur_col > len) cur_col = len;
    if (len >= MAXLINE - 1) return;
    memmove(&lines[cur_line][cur_col + 1],
            &lines[cur_line][cur_col],
            (size_t)(len - cur_col + 1));
    lines[cur_line][cur_col] = (char)c;
    cur_col++;
    modified = 1;
}

static void split_line(void)
{
    char tailbuf[MAXLINE];
    int len, tail;
    if (nlines >= MAXLINES) return;
    len = (int)strlen(lines[cur_line]);
    if (cur_col > len) cur_col = len;
    tail = len - cur_col;

    memcpy(tailbuf, &lines[cur_line][cur_col], (size_t)(tail + 1));
    lines[cur_line][cur_col] = '\0';

    memmove(&lines[cur_line + 2], &lines[cur_line + 1],
            (size_t)((nlines - cur_line - 1) * MAXLINE));

    memcpy(lines[cur_line + 1], tailbuf, (size_t)(tail + 1));

    nlines++;
    cur_line++;
    cur_col = 0;
    modified = 1;
}

static void join(int a, int b)
{
    int alen = (int)strlen(lines[a]);
    int blen = (int)strlen(lines[b]);
    int i;
    if (alen + blen >= MAXLINE - 1) return;
    memcpy(&lines[a][alen], lines[b], (size_t)(blen + 1));
    for (i = b; i < nlines - 1; i++) {
        size_t l = strlen(lines[i + 1]) + 1;
        memcpy(lines[i], lines[i + 1], l);
    }
    nlines--;
    cur_line = a;
    cur_col  = alen;
    modified = 1;
}

static void del_back(void)
{
    int len = (int)strlen(lines[cur_line]);
    if (cur_col > 0) {
        memmove(&lines[cur_line][cur_col - 1],
                &lines[cur_line][cur_col],
                (size_t)(len - cur_col + 1));
        cur_col--;
        modified = 1;
    } else if (cur_line > 0) {
        join(cur_line - 1, cur_line);
    }
}

static void del_forward(void)
{
    int len = (int)strlen(lines[cur_line]);
    if (cur_col < len) {
        memmove(&lines[cur_line][cur_col],
                &lines[cur_line][cur_col + 1],
                (size_t)(len - cur_col));
        modified = 1;
    } else if (cur_line < nlines - 1) {
        join(cur_line, cur_line + 1);
    }
}

/* ---------- input ---------- */

static int shift_down(void)
{
    return tui_shift_down();
}

static void move_arrow(int code)
{
    int shift = shift_down();
    switch (code) {
    case 72: if (cur_line > 0) { cur_line--; clamp_col(); } break;
    case 80: if (cur_line < nlines - 1) { cur_line++; clamp_col(); } break;
    case 75:
        if (shift) word_left();
        else if (cur_col > 0) cur_col--;
        else if (cur_line > 0) {
            cur_line--;
            cur_col = (int)strlen(lines[cur_line]);
        }
        break;
    case 77:
        if (shift) word_right();
        else if (cur_col < (int)strlen(lines[cur_line])) cur_col++;
        else if (cur_line < nlines - 1) { cur_line++; cur_col = 0; }
        break;
    case 71: cur_col = 0; break;
    case 79: cur_col = (int)strlen(lines[cur_line]); break;
    case 73:
        cur_line -= SCRH - 1;
        if (cur_line < 0) cur_line = 0;
        clamp_col();
        break;
    case 81:
        cur_line += SCRH - 1;
        if (cur_line >= nlines) cur_line = nlines - 1;
        clamp_col();
        break;
    }
}

static void prompt_line(const char *prompt, char *dest, int maxlen)
{
    int len = 0, c;
    tui_puts(prompt);
    for (;;) {
        c = tui_getch();
        if (c == 0 || c == 224) { tui_getch(); continue; }
        if (c == 13) break;
        if (c == 8) {
            if (len > 0) { len--; tui_putc('\b'); tui_putc(' '); tui_putc('\b'); }
            continue;
        }
        if (c == 27) { dest[0] = '\0'; return; }
        if (c >= 32 && c < 127 && len < maxlen - 1) {
            dest[len++] = (char)c;
            tui_putc(c);
        }
    }
    dest[len] = '\0';
}

/* ---------- save with name prompt (if untitled) ---------- */

static int save_current(void)
{
    if (!filename[0]) {
        char path[260];
        prompt_bar("Save as: ");
        path[0] = '\0';
        prompt_line("", path, sizeof path);
        tui_color(TUI_LGRAY, TUI_BLACK);
        if (!path[0]) return 0;
        strncpy(filename, path, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    }
    return save_file();
}

/* ---------- Ctrl+N: new named document ---------- */

static void new_document(void)
{
    char path[260];
    FILE *probe;

    clear_mark();

    if (modified) {
        int k;
        prompt_bar("Save changes? (y/n/c) ");
        k = tui_getch();
        tui_color(TUI_LGRAY, TUI_BLACK);
        if (k == 'c' || k == 'C' || k == 27) return;
        if (k == 'y' || k == 'Y') {
            if (!save_current()) return;
        }
    }

    prompt_bar("New file: ");
    path[0] = '\0';
    prompt_line("", path, sizeof path);
    tui_color(TUI_LGRAY, TUI_BLACK);
    if (!path[0]) return;

    nlines = 1;
    lines[0][0] = '\0';
    modified = 0;
    top_line = 0;
    left_col = 0;
    cur_line = 0;
    cur_col  = 0;

    strncpy(filename, path, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';

    probe = fopen(filename, "rb");
    if (probe) {
        fclose(probe);
        load_file(filename);
        cur_line = 0;
        cur_col  = 0;
    } else {
        FILE *out = fopen(filename, "wb");
        if (out) fclose(out);
    }
}

/* ---------- F4: save, spellcheck, reload ---------- */

static int spell_and_reload(void)
{
    clear_mark();

    if (!filename[0]) {
        if (!save_current()) {
            status_msg("Need a filename to spellcheck.");
            tui_getch();
            return 0;
        }
    }
    if (!save_file()) {
        status_msg("Save failed.");
        tui_getch();
        return 0;
    }

    snprintf(tmp_path, sizeof tmp_path, "%s.tmp", filename);

    tui_restore();
    tui_clear();

    {
        char cmd[800];
        snprintf(cmd, sizeof cmd, "spell.exe \"%s\" \"%s\" \"%s\"",
                 dict_path, filename, tmp_path);
        system(cmd);
    }

    tui_init();
    tui_clear();

    {
        FILE *probe = fopen(tmp_path, "rb");
        if (probe) {
            fclose(probe);
            load_file(tmp_path);
            remove(tmp_path);
        } else {
            load_file(filename);
        }
    }
    top_line = 0;
    left_col = 0;
    cur_line = 0;
    cur_col  = 0;
    modified = 0;
    return 1;
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    int ch;

    if (argc > 1) {
        strncpy(filename, argv[1], sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    }

    tui_init();
    tui_clear();

    lines[0][0] = '\0';
    nlines = 1;
    if (filename[0]) load_file(filename);

    for (;;) {
        render();
        ch = tui_getch();
        if (ch == 0 || ch == 224) {
            int code = tui_getch();
            if (code == 60) {
                clear_mark();
                if (cur_line < nlines - 1) join(cur_line, cur_line + 1);
            } else if (code == 61) {
                clear_mark();
                save_current();
            } else if (code == 62) {
                spell_and_reload();
            } else if (code == 83) {
                clear_mark();
                del_forward();
            } else {
                move_arrow(code);
            }
            continue;
        }
        switch (ch) {
        case 2:                                   /* Ctrl+B */
            if (mark_active) {
                mark.el = cur_line;
                mark.ec = cur_col;
                mark_active = 0;
                mark_exists = 1;
                copy_mark_to_clip();
            } else {
                mark.sl = cur_line;
                mark.sc = cur_col;
                mark.el = cur_line;
                mark.ec = cur_col;
                mark_active = 1;
                mark_exists = 0;
                clip_clear();
            }
            break;
        case 24:                                  /* Ctrl+X: cut */
            if (mark_exists) {
                copy_mark_to_clip();
                delete_mark();
                clear_mark();
            }
            break;
        case 16:                                  /* Ctrl+P: paste */
            if (clip_len > 0) paste_clip();
            clear_mark();
            break;
        case 6:                                   /* Ctrl+F: find */
            {
                char needle[128];
                needle[0] = '\0';
                prompt_bar("Find: ");
                prompt_line("", needle, sizeof needle);
                tui_color(TUI_LGRAY, TUI_BLACK);
                if (needle[0]) find_next(needle);
            }
            break;
        case 14:                                  /* Ctrl+N */
            new_document();
            break;
        case 27:                                  /* Esc */
            if (mark_active || mark_exists) {
                clear_mark();
                break;
            }
            if (modified) {
                int k;
                prompt_bar("Save before quit? (y/n) ");
                k = tui_getch();
                tui_color(TUI_LGRAY, TUI_BLACK);
                if (k == 'y' || k == 'Y') save_current();
            }
            tui_cursor(1, 0);
            tui_clear();
            tui_restore();
            tui_gotoxy(1, 1);
            return 0;
        case 13:
            clear_mark();
            split_line();
            break;
        case 8:
            clear_mark();
            del_back();
            break;
        default:
            if (ch >= 32 && ch < 127) {
                clear_mark();
                insert_char(ch);
            }
            break;
        }
    }
}