/* tui_console.c - Windows console backend for tui.h
 * compile: tcc tiny.c tui_console.c -luser32 -o tiny.exe
 */

#include <stdio.h>
#include <conio.h>
#include <windows.h>
#include "tui.h"

#ifndef ENABLE_QUICK_EDIT_MODE
#define ENABLE_QUICK_EDIT_MODE 0x0040
#endif
#ifndef ENABLE_EXTENDED_FLAGS
#define ENABLE_EXTENDED_FLAGS 0x0080
#endif
#ifndef ENABLE_WRAP_AT_EOL_OUTPUT
#define ENABLE_WRAP_AT_EOL_OUTPUT 0x0002
#endif

static HANDLE hOut, hIn;
static WORD   cur_attr = 0x07;

void tui_init(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    SMALL_RECT sr;
    COORD sz;
    DWORD mode;
    HWND hwnd;

    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hIn  = GetStdHandle(STD_INPUT_HANDLE);

    GetConsoleMode(hIn, &mode);
    mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_QUICK_EDIT_MODE);
    mode |= ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(hIn, mode);

    GetConsoleMode(hOut, &mode);
    mode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
    SetConsoleMode(hOut, mode);

    GetConsoleScreenBufferInfo(hOut, &csbi);
    if (csbi.srWindow.Bottom - csbi.srWindow.Top + 1 > 24) {
        sr.Left = 0; sr.Top = 0; sr.Right = 79; sr.Bottom = 24;
        SetConsoleWindowInfo(hOut, TRUE, &sr);
    }
    sz.X = 80; sz.Y = 25;
    SetConsoleScreenBufferSize(hOut, sz);

    hwnd = GetConsoleWindow();
    if (hwnd) {
        LONG s = GetWindowLong(hwnd, GWL_STYLE);
        s &= ~WS_SIZEBOX;
        s &= ~WS_MAXIMIZEBOX;
        SetWindowLong(hwnd, GWL_STYLE, s);
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    tui_cursor(1, 1);
    tui_color(TUI_LGRAY, TUI_BLACK);
}

void tui_restore(void)
{
    CONSOLE_CURSOR_INFO ci;
    DWORD mode;
    HWND hwnd;

    GetConsoleMode(hIn, &mode);
    mode |= ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT;
    SetConsoleMode(hIn, mode);

    GetConsoleMode(hOut, &mode);
    mode |= ENABLE_WRAP_AT_EOL_OUTPUT;
    SetConsoleMode(hOut, mode);

    GetConsoleCursorInfo(hOut, &ci);
    ci.bVisible = TRUE; ci.dwSize = 25;
    SetConsoleCursorInfo(hOut, &ci);

    hwnd = GetConsoleWindow();
    if (hwnd) {
        LONG s = GetWindowLong(hwnd, GWL_STYLE);
        s |= WS_SIZEBOX | WS_MAXIMIZEBOX;
        SetWindowLong(hwnd, GWL_STYLE, s);
    }

    SetConsoleTextAttribute(hOut, 0x07);
    setvbuf(stdout, NULL, _IOLBF, 0);
}

void tui_clear(void)
{
    COORD home = {0,0};
    DWORD n;
    FillConsoleOutputCharacter(hOut, ' ', 80 * 25, home, &n);
    FillConsoleOutputAttribute(hOut, cur_attr, 80 * 25, home, &n);
    SetConsoleCursorPosition(hOut, home);
}

void tui_gotoxy(int x, int y)
{
    COORD c;
    c.X = (SHORT)(x - 1);
    c.Y = (SHORT)(y - 1);
    SetConsoleCursorPosition(hOut, c);
}

void tui_color(int fg, int bg)
{
    cur_attr = (WORD)(fg | (bg << 4));
    SetConsoleTextAttribute(hOut, cur_attr);
}

void tui_putc(int c) { putchar(c); }
void tui_puts(const char *s) { fputs(s, stdout); }

int tui_getch(void) { return _getch(); }

int tui_shift_down(void)
{
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

void tui_cursor(int visible, int block)
{
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(hOut, &ci);
    ci.bVisible = visible ? TRUE : FALSE;
    ci.dwSize   = block ? 100 : 25;
    SetConsoleCursorInfo(hOut, &ci);
}