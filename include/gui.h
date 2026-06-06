#ifndef GUI_H
#define GUI_H

#include "stdint.h"

/* Ekran çözünürlüğü (VESA 320x200x32 veya VGA 320x200) */
#define GUI_WIDTH   320
#define GUI_HEIGHT  200
#define GUI_BPP     4       /* 4 byte per pixel (32-bit) */

/* Renkler (ARGB) */
#define GUI_BLACK       0x00000000
#define GUI_WHITE       0xFFFFFFFF
#define GUI_RED         0xFFFF0000
#define GUI_GREEN       0xFF00FF00
#define GUI_BLUE        0xFF0000FF
#define GUI_YELLOW      0xFFFFFF00
#define GUI_CYAN        0xFF00FFFF
#define GUI_MAGENTA     0xFFFF00FF
#define GUI_DARK_GREY   0xFF444444
#define GUI_LIGHT_GREY  0xFFCCCCCC
#define GUI_ORANGE      0xFFFF8800
#define GUI_DARK_BLUE   0xFF000088

/* Pencere */
#define MAX_WINDOWS     8
#define WIN_TITLE_HEIGHT 18

typedef struct {
    int      x, y;
    int      w, h;
    uint32_t title_color;
    uint32_t bg_color;
    char     title[32];
    int      visible;
    int      focused;
} window_t;

/* Fonksiyonlar */
void gui_init(void);
void gui_clear(uint32_t color);
void gui_put_pixel(int x, int y, uint32_t color);
void gui_draw_rect(int x, int y, int w, int h, uint32_t color);
void gui_fill_rect(int x, int y, int w, int h, uint32_t color);
void gui_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void gui_draw_circle(int cx, int cy, int r, uint32_t color);
void gui_fill_circle(int cx, int cy, int r, uint32_t color);
void gui_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void gui_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void gui_render(void);

/* Pencere yönetimi */
window_t *gui_create_window(const char *title, int x, int y, int w, int h);
void      gui_draw_window(window_t *win);
void      gui_draw_desktop(void);

#endif
