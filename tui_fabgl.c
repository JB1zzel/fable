/* tui_fabgl.c - FabGL backend for Fable on TTGO VGA32
 * compile with Arduino IDE / PlatformIO for ESP32
 */

#include <fabgl.h>
#include "tui.h"

static fabgl::VGAController VGAController;
static fabgl::Terminal      Terminal;
static fabgl::Keyboard      Keyboard;
static fabgl::PS2Controller PS2Controller;

/* 16-colour palette, matching the PC/TUI_* indices */
static const fabgl::RGB888 palette[16] = {
    /*  0 BLACK    */ fabgl::RGB888(0,0,0),
    /*  1 BLUE     */ fabgl::RGB888(0,0,170),
    /*  2 GREEN    */ fabgl::RGB888(0,170,0),
    /*  3 CYAN     */ fabgl::RGB888(0,170,170),
    /*  4 RED      */ fabgl::RGB888(170,0,0),
    /*  5 MAGENTA  */ fabgl::RGB888(170,0,170),
    /*  6 BROWN    */ fabgl::RGB888(170,85,0),
    /*  7 LGRAY    */ fabgl::RGB888(170,170,170),
    /*  8 DGRAY    */ fabgl::RGB888(85,85,85),
    /*  9 LBLUE    */ fabgl::RGB888(85,85,255),
    /* 10 LGREEN   */ fabgl::RGB888(85,255,85),
    /* 11 LCYAN    */ fabgl::RGB888(85,255,255),
    /* 12 LRED     */ fabgl::RGB888(255,85,85),
    /* 13 LMAGENTA */ fabgl::RGB888(255,85,255),
    /* 14 YELLOW   */ fabgl::RGB888(255,255,85),
    /* 15 WHITE    */ fabgl::RGB888(255,255,255)
};

void tui_init(void)
{
    PS2Controller.begin(PS2Preset::KeyboardPort0);
    Keyboard.begin();
    VGAController.begin();
    VGAController.setResolution(VGA_640x480_60Hz);
    Terminal.begin(&VGAController);
    Terminal.setBackgroundColor(palette[0]);
    Terminal.setForegroundColor(palette[7]);
    Terminal.clear();
    Terminal.setCursorVisible(true);
}

void tui_restore(void)
{
    Terminal.setCursorVisible(true);
    VGAController.end();
}

void tui_clear(void)
{
    Terminal.clear();
}

void tui_gotoxy(int x, int y)
{
    Terminal.setCursorPos(x - 1, y - 1);
}

void tui_color(int fg, int bg)
{
    if (fg < 0 || fg > 15) fg = 7;
    if (bg < 0 || bg > 15) bg = 0;
    Terminal.setForegroundColor(palette[fg]);
    Terminal.setBackgroundColor(palette[bg]);
}

void tui_putc(int c)
{
    Terminal.write((char)c);
}

void tui_puts(const char *s)
{
    while (*s) Terminal.write(*s++);
}

void tui_cursor(int visible, int block)
{
    Terminal.setCursorVisible(visible ? true : false);
}

/* Map FabGL VirtualKey to the PC scancode fableGL.c expects.
 * fableGL.c uses the two-call pattern: first 0 or 224, then the scancode.
 * We buffer the scancode and return 0 first, then the code.
 */
static int  pending_code = -1;

static int vk_to_scancode(int vk)
{
    switch (vk) {
    case VK_UP:       return 72;
    case VK_DOWN:     return 80;
    case VK_LEFT:     return 75;
    case VK_RIGHT:    return 77;
    case VK_HOME:     return 71;
    case VK_END:      return 79;
    case VK_PAGEUP:   return 73;
    case VK_PAGEDOWN: return 81;
    case VK_DELETE:   return 83;
    case VK_F1:       return 59;
    case VK_F2:       return 60;
    case VK_F3:       return 61;
    case VK_F4:       return 62;
    case VK_F5:       return 63;
    default:          return -1;
    }
}

int tui_getch(void)
{
    if (pending_code >= 0) {
        int c = pending_code;
        pending_code = -1;
        return c;
    }

    for (;;) {
        VirtualKey vk;
        if (Keyboard.getNextVirtualKey(&vk)) {
            int code = vk_to_scancode(vk.VK);
            if (code >= 0) {
                pending_code = code;
                return 0;           /* fableGL.c sees 0, then the code */
            }
            if (vk.VK == VK_RETURN)  return 13;
            if (vk.VK == VK_BACK)    return 8;
            if (vk.VK == VK_ESCAPE)  return 27;
            if (vk.VK == VK_TAB)     return 9;
            if (vk.ASCII >= 32 && vk.ASCII < 127) return vk.ASCII;
        }
        delay(1);
    }
}

int tui_shift_down(void)
{
    return (Keyboard.isVKDown(VK_LSHIFT) || Keyboard.isVKDown(VK_RSHIFT)) ? 1 : 0;
}

/* No exe folder on ESP32 — return "". dict.smd lives on SD. */
void tui_exe_dir(char *out, int len)
{
    if (len > 0) out[0] = '\0';
}