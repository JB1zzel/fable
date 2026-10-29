/* tiny.c - tiny text editor for TCC / Windows CMD
 * compile: tcc editor.c -luser32
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>
#include <windows.h>

#ifndef ENABLE_QUICK_EDIT_MODE
#define ENABLE_QUICK_EDIT_MODE 0x0040
#endif
#ifndef ENABLE_EXTENDED_FLAGS
#define ENABLE_EXTENDED_FLAGS 0x0080
#endif
#ifndef ENABLE_MOUSE_INPUT
#define ENABLE_MOUSE_INPUT 0x0010
#endif
#ifndef ENABLE_WRAP_AT_EOL_OUTPUT
#define ENABLE_WRAP_AT_EOL_OUTPUT 0x0002
#endif

#define MAXLINES 4096
#define MAXLINE  1024
#define SCRW     80
#define SCRH     24

#define IS_WORD(c) (isalnum((unsigned char)(c)) || (c) == '_')

static char  lines[MAXLINES][MAXLINE];
static int   nlines;
static int   cur_line, cur_col;
static int   top_line = 0;
static int   modified;
static char  filename[260];
static HANDLE hOut, hIn;

/* ---------- console ---------- */

static void con_cursor_block(int on)
{
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(hOut, &ci);
    ci.bVisible = on ? TRUE : FALSE;
    ci.dwSize   = 100;
    SetConsoleCursorInfo(hOut, &ci);
}

static void lock_window(void)
{
    HWND hwnd = GetConsoleWindow();
    if (hwnd) {
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        style &= ~WS_SIZEBOX;
        style &= ~WS_MAXIMIZEBOX;
        SetWindowLong(hwnd, GWL_STYLE, style);
    }
}

static void init_console(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD sz;
    SMALL_RECT sr;
    DWORD imode, omode;

    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hIn  = GetStdHandle(STD_INPUT_HANDLE);

    GetConsoleMode(hIn, &imode);
    imode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT
             | ENABLE_QUICK_EDIT_MODE | ENABLE_MOUSE_INPUT);
    imode |= ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(hIn, imode);

    GetConsoleMode(hOut, &omode);
    omode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
    SetConsoleMode(hOut, omode);

    GetConsoleScreenBufferInfo(hOut, &csbi);
    if (csbi.srWindow.Bottom - csbi.srWindow.Top + 1 > SCRH ||
        csbi.srWindow.Right  - csbi.srWindow.Left + 1 > SCRW) {
        sr.Left = 0; sr.Top = 0;
        sr.Right = (SHORT)(SCRW - 1);
        sr.Bottom = (SHORT)SCRH;
        SetConsoleWindowInfo(hOut, TRUE, &sr);
    }

    sz.X = (SHORT)SCRW;
    sz.Y = (SHORT)(SCRH + 1);
    SetConsoleScreenBufferSize(hOut, sz);

    setvbuf(stdout, NULL, _IONBF, 0);

    lock_window();
    con_cursor_block(1);
}

static void con_clrscr(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD home = {0, 0};
    DWORD written, cells;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    cells = (DWORD)csbi.dwSize.X * (DWORD)(SCRH + 1);
    FillConsoleOutputCharacter(hOut, ' ', cells, home, &written);
    FillConsoleOutputAttribute(hOut, csbi.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(hOut, home);
}

static void con_gotoxy(int x, int y)
{
    COORD c;
    c.X = (SHORT)(x - 1);
    c.Y = (SHORT)(y - 1);
    SetConsoleCursorPosition(hOut, c);
}

static void con_textattr(WORD attr) { SetConsoleTextAttribute(hOut, attr); }

static void con_puts_line(const char *s)
{
    int n = (int)strlen(s);
    if (n > SCRW) n = SCRW;
    if (n) fwrite(s, 1, (size_t)n, stdout);
    while (n < SCRW) { putchar(' '); n++; }
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

/* ---------- render ---------- */

static void put_status(void)
{
    char left[SCRW + 1];
    char right[SCRW + 1];
    int w = count_words();
    int len, rlen;

    snprintf(left, sizeof left, " %s  Ln %d, Col %d  Words: %d%s",
             filename[0] ? filename : "(unnamed)",
             cur_line + 1, cur_col + 1, w,
             modified ? " [modified]" : "");
    left[SCRW] = '\0';

    snprintf(right, sizeof right, "F3 SAVE  ESC EXIT ");
    rlen = (int)strlen(right);

    len = (int)strlen(left);
    if (len + rlen > SCRW) {
        len = SCRW - rlen - 1;
        if (len < 0) len = 0;
        left[len] = '\0';
    }

    con_gotoxy(1, SCRH + 1);
    con_textattr(0x70);
    {
        int i, gap = SCRW - rlen - len;
        fwrite(left, 1, (size_t)len, stdout);
        for (i = 0; i < gap; i++) putchar(' ');
        fwrite(right, 1, (size_t)rlen, stdout);
    }
    con_textattr(0x07);
}

static void render(void)
{
    int i;

    if (cur_line < top_line) top_line = cur_line;
    if (cur_line >= top_line + SCRH) top_line = cur_line - SCRH + 1;
    if (top_line < 0) top_line = 0;
    if (top_line > nlines - 1) top_line = nlines - 1;

    con_cursor_block(0);

    for (i = 0; i < SCRH; i++) {
        int ln = top_line + i;
        con_gotoxy(1, i + 1);
        if (ln < nlines) con_puts_line(lines[ln]);
        else             con_puts_line("");
    }
    put_status();

    con_gotoxy(cur_col + 1, cur_line - top_line + 1);
    con_cursor_block(1);
}

/* ---------- cursor ---------- */

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
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
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
    printf("%s", prompt);
    for (;;) {
        c = _getch();
        if (c == 0 || c == 224) { _getch(); continue; }
        if (c == 13) break;
        if (c == 8) {
            if (len > 0) { len--; putchar('\b'); putchar(' '); putchar('\b'); }
            continue;
        }
        if (c == 27) { dest[0] = '\0'; return; }
        if (c >= 32 && c < 127 && len < maxlen - 1) {
            dest[len++] = (char)c;
            putchar(c);
        }
    }
    dest[len] = '\0';
    putchar('\n');
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    int ch;

    if (argc > 1) {
        strncpy(filename, argv[1], sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    } else {
        prompt_line("File: ", filename, sizeof filename);
    }

    init_console();
    con_clrscr();
    con_textattr(0x07);

    lines[0][0] = '\0';
    nlines = 1;
    if (filename[0]) load_file(filename);

    for (;;) {
        render();
        ch = _getch();
        if (ch == 0 || ch == 224) {
            int code = _getch();
            if (code == 60) {
                if (cur_line < nlines - 1) join(cur_line, cur_line + 1);
            } else if (code == 61) {
                if (filename[0]) save_file();
            } else if (code == 83) {
                del_forward();
            } else {
                move_arrow(code);
            }
            continue;
        }
        switch (ch) {
        case 27:
            if (modified && filename[0]) {
                int k;
                con_gotoxy(1, SCRH + 1);
                con_textattr(0x70);
                {
                    char msg[SCRW + 1];
                    int i;
                    snprintf(msg, sizeof msg, " Save before quit? (y/n) ");
                    for (i = (int)strlen(msg); i < SCRW; i++) msg[i] = ' ';
                    msg[SCRW] = '\0';
                    fwrite(msg, 1, SCRW, stdout);
                }
                con_textattr(0x07);
                k = _getch();
                if (k == 'y' || k == 'Y') save_file();
            }
            /* restore default cursor before exit */
            {
                CONSOLE_CURSOR_INFO ci;
                GetConsoleCursorInfo(hOut, &ci);
                ci.bVisible = TRUE;
                ci.dwSize   = 25;
                SetConsoleCursorInfo(hOut, &ci);
            }
            con_clrscr();
            con_textattr(0x07);
            con_gotoxy(1, 1);
            return 0;
        case 13: split_line(); break;
        case 8:  del_back();   break;
        default:
            if (ch >= 32 && ch < 127) insert_char(ch);
            break;
        }
    }
}