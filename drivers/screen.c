#include "../include/screen.h"
#include <stdint.h>

/* VGA bellek adresi */
static volatile uint16_t *vga_buffer = (uint16_t *)VGA_ADDRESS;

/* Cursor pozisyonu */
static int cursor_col = 0;
static int cursor_row = 0;

/* Mevcut renk (varsayılan: beyaz üzerine siyah) */
static uint8_t current_color = (COLOR_BLACK << 4) | COLOR_WHITE;

/* ============================================================
 * Yardımcı: VGA karakter + renk birleştir
 * ============================================================ */
static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

/* ============================================================
 * Port I/O: cursor güncellemek için
 * ============================================================ */
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* ============================================================
 * Hardware cursor güncelle
 * ============================================================ */
static void update_cursor(void) {
    uint16_t pos = cursor_row * VGA_COLS + cursor_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/* ============================================================
 * Ekranı başlat
 * ============================================================ */
void screen_init(void) {
    screen_clear();
}

/* ============================================================
 * Ekranı temizle
 * ============================================================ */
void screen_clear(void) {
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++) {
        vga_buffer[i] = vga_entry(' ', current_color);
    }
    cursor_col = 0;
    cursor_row = 0;
    update_cursor();
}

/* ============================================================
 * Renk ayarla
 * ============================================================ */
void screen_set_color(vga_color_t fg, vga_color_t bg) {
    current_color = ((uint8_t)bg << 4) | (uint8_t)fg;
}

/* ============================================================
 * Ekranı yukarı kaydır (scroll)
 * ============================================================ */
void screen_scroll(void) {
    /* Tüm satırları bir yukarı taşı */
    for (int row = 0; row < VGA_ROWS - 1; row++) {
        for (int col = 0; col < VGA_COLS; col++) {
            vga_buffer[row * VGA_COLS + col] = vga_buffer[(row + 1) * VGA_COLS + col];
        }
    }
    /* Son satırı temizle */
    for (int col = 0; col < VGA_COLS; col++) {
        vga_buffer[(VGA_ROWS - 1) * VGA_COLS + col] = vga_entry(' ', current_color);
    }
    cursor_row = VGA_ROWS - 1;
}

/* ============================================================
 * Tek karakter yaz
 * ============================================================ */
void screen_putchar(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\t') {
        cursor_col = (cursor_col + 8) & ~7;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            vga_buffer[cursor_row * VGA_COLS + cursor_col] = vga_entry(' ', current_color);
        }
    } else {
        vga_buffer[cursor_row * VGA_COLS + cursor_col] = vga_entry(c, current_color);
        cursor_col++;
    }

    /* Satır sonu kontrolü */
    if (cursor_col >= VGA_COLS) {
        cursor_col = 0;
        cursor_row++;
    }

    /* Sayfa sonu: scroll */
    if (cursor_row >= VGA_ROWS) {
        screen_scroll();
    }

    update_cursor();
}

/* ============================================================
 * String yaz
 * ============================================================ */
void screen_print(const char *str) {
    while (*str) {
        screen_putchar(*str++);
    }
}

/* ============================================================
 * String + newline yaz
 * ============================================================ */
void screen_println(const char *str) {
    screen_print(str);
    screen_putchar('\n');
}

/* ============================================================
 * Hexadecimal sayı yaz (0x... formatında)
 * ============================================================ */
void screen_print_hex(uint32_t value) {
    const char hex_chars[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    buf[10] = '\0';
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex_chars[value & 0xF];
        value >>= 4;
    }
    screen_print(buf);
}

/* ============================================================
 * Integer yaz
 * ============================================================ */
void screen_print_int(int32_t value) {
    if (value < 0) {
        screen_putchar('-');
        value = -value;
    }
    if (value == 0) {
        screen_putchar('0');
        return;
    }
    char buf[12];
    int i = 11;
    buf[i] = '\0';
    while (value > 0 && i > 0) {
        buf[--i] = '0' + (value % 10);
        value /= 10;
    }
    screen_print(&buf[i]);
}

/* ============================================================
 * Cursor pozisyonunu ayarla
 * ============================================================ */
void screen_set_cursor(int col, int row) {
    cursor_col = col;
    cursor_row = row;
    update_cursor();
}
