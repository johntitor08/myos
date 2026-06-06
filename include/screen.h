#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

/* VGA Text Mode sabitleri */
#define VGA_ADDRESS     0xB8000
#define VGA_COLS        80
#define VGA_ROWS        25

/* Renkler */
typedef enum {
    COLOR_BLACK         = 0,
    COLOR_BLUE          = 1,
    COLOR_GREEN         = 2,
    COLOR_CYAN          = 3,
    COLOR_RED           = 4,
    COLOR_MAGENTA       = 5,
    COLOR_BROWN         = 6,
    COLOR_LIGHT_GREY    = 7,
    COLOR_DARK_GREY     = 8,
    COLOR_LIGHT_BLUE    = 9,
    COLOR_LIGHT_GREEN   = 10,
    COLOR_LIGHT_CYAN    = 11,
    COLOR_LIGHT_RED     = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_YELLOW        = 14,
    COLOR_WHITE         = 15,
} vga_color_t;

/* Fonksiyon prototipleri */
void screen_init(void);
void screen_clear(void);
void screen_putchar(char c);
void screen_print(const char *str);
void screen_println(const char *str);
void screen_set_color(vga_color_t fg, vga_color_t bg);
void screen_print_hex(uint32_t value);
void screen_print_int(int32_t value);
void screen_set_cursor(int col, int row);
void screen_scroll(void);

#endif
