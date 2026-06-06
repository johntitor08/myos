#include "../include/gui.h"
#include "../include/memory.h"
#include "../include/screen.h"
#include "../include/timer.h"

/* ============================================================
 * MyOS GUI — VESA VBE 320×200 32-bit framebuffer
 *
 * Bootloader'ı VESA modunu set edecek şekilde güncelleyeceğiz.
 * Şimdilik VGA 320×200 mod 13h kullanıyoruz (256 renk ama
 * framebuffer API'si aynı).
 *
 * VGA Mode 13h: 0xA0000, 320×200, 1 byte/pixel (palette)
 * Bizim yöntemimiz: 32-bit çift buffer + son render'da palette map
 * ============================================================ */

#define FRAMEBUFFER  ((uint8_t *)0xA0000)
#define BUF_SIZE     (GUI_WIDTH * GUI_HEIGHT)

/* 32-bit çift buffer (off-screen) */
static uint32_t backbuffer[GUI_WIDTH * GUI_HEIGHT];

/* Pencere tablosu (gui_init bunları sıfırladığı için burada tanımlı) */
static window_t windows[MAX_WINDOWS];
static int      win_count = 0;

/* VGA 256-renk paleti (basit 6-bit RGB dönüşümü) */
static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }

/* ============================================================
 * 5-6-5 renk → VGA palette index (en yakın) — basit ağaçlama
 * ============================================================ */
static uint8_t rgb32_to_vga(uint32_t c) {
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >>  8) & 0xFF;
    uint8_t b =  c        & 0xFF;
    /* 6-bit renk uzayı: 0-63 */
    return (uint8_t)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
}

/* ============================================================
 * VGA Mode 13h başlat (BIOS INT 10h kullanamayız, doğrudan yaz)
 * ============================================================ */
static void vga_mode13_init(void) {
    /* Sequencer */
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);
    outb(0x3C4, 0x01); outb(0x3C5, 0x01);
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F);
    outb(0x3C4, 0x03); outb(0x3C5, 0x00);
    outb(0x3C4, 0x04); outb(0x3C5, 0x0E);

    /* Misc output: 25MHz clock, page select */
    outb(0x3C2, 0x63);

    /* CRTC - Mode 13h değerleri */
    outb(0x3D4, 0x11); outb(0x3D5, 0x0E);  /* unprotect */
    static const uint8_t crtc[] = {
        0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,
        0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,
        0x9C,0x0E,0x8F,0x28,0x40,0x96,0xB9,0xA3,0xFF
    };
    for (int i = 0; i < 25; i++) { outb(0x3D4, (uint8_t)i); outb(0x3D5, crtc[i]); }

    /* GC registers */
    static const uint8_t gc[] = {0,0,0,0,0,0x40,0x05,0x0F,0xFF};
    for (int i = 0; i < 9; i++) { outb(0x3CE, (uint8_t)i); outb(0x3CF, gc[i]); }

    /* AC registers */
    inb(0x3DA);
    static const uint8_t ac[] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,
        0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
        0x41,0x00,0x0F,0x00,0x00
    };
    for (int i = 0; i < 21; i++) { outb(0x3C0, (uint8_t)i); outb(0x3C0, ac[i]); }
    outb(0x3C0, 0x20);

    /* Paleti yükle: 216-renk web paleti */
    outb(0x3C8, 0);
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++) {
                outb(0x3C9, (uint8_t)(r * 10));
                outb(0x3C9, (uint8_t)(g * 10));
                outb(0x3C9, (uint8_t)(b * 10));
            }
    /* Ek saf renkler */
    for (int i = 216; i < 256; i++) {
        uint8_t v = (uint8_t)((i - 216) * 4);
        outb(0x3C9, v); outb(0x3C9, v); outb(0x3C9, v);
    }
}

/* ============================================================
 * Küçük 5×7 font (sadece ASCII 32-90, çok basit)
 * ============================================================ */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
};

/* ============================================================
 * GUI başlat
 * ============================================================ */
void gui_init(void) {
    vga_mode13_init();
    memset(backbuffer, 0, sizeof(backbuffer));
    win_count = 0;   /* her oturumda pencereleri sıfırla (aksi halde
                        tekrarlanan 'gui' MAX_WINDOWS'u doldurup NULL döndürür) */
    screen_println("[GUI] VGA Mode 13h hazir. 320x200x8 (palette)");
}

/* ============================================================
 * Piksel koy
 * ============================================================ */
void gui_put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= GUI_WIDTH || y < 0 || y >= GUI_HEIGHT) return;
    backbuffer[y * GUI_WIDTH + x] = color;
}

/* ============================================================
 * Ekranı doldur
 * ============================================================ */
void gui_clear(uint32_t color) {
    for (int i = 0; i < GUI_WIDTH * GUI_HEIGHT; i++)
        backbuffer[i] = color;
}

/* ============================================================
 * Dikdörtgen çiz (sadece kenar)
 * ============================================================ */
void gui_draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int i = x; i < x+w; i++) { gui_put_pixel(i, y, color); gui_put_pixel(i, y+h-1, color); }
    for (int i = y; i < y+h; i++) { gui_put_pixel(x, i, color); gui_put_pixel(x+w-1, i, color); }
}

/* ============================================================
 * Dolu dikdörtgen
 * ============================================================ */
void gui_fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int row = y; row < y+h; row++)
        for (int col = x; col < x+w; col++)
            gui_put_pixel(col, row, color);
}

/* ============================================================
 * Bresenham çizgi
 * ============================================================ */
void gui_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1-x0, dy = y1-y0;
    int sx = dx>0?1:-1, sy = dy>0?1:-1;
    dx = dx<0?-dx:dx; dy = dy<0?-dy:dy;
    int err = dx-dy;
    while (1) {
        gui_put_pixel(x0, y0, color);
        if (x0==x1 && y0==y1) break;
        int e2 = 2*err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* ============================================================
 * Bresenham daire
 * ============================================================ */
void gui_draw_circle(int cx, int cy, int r, uint32_t color) {
    int x=r, y=0, err=0;
    while (x >= y) {
        gui_put_pixel(cx+x, cy+y, color); gui_put_pixel(cx-x, cy+y, color);
        gui_put_pixel(cx+x, cy-y, color); gui_put_pixel(cx-x, cy-y, color);
        gui_put_pixel(cx+y, cy+x, color); gui_put_pixel(cx-y, cy+x, color);
        gui_put_pixel(cx+y, cy-x, color); gui_put_pixel(cx-y, cy-x, color);
        y++;
        if (err <= 0) { err += 2*y+1; }
        else { x--; err += 2*(y-x)+1; }
    }
}

void gui_fill_circle(int cx, int cy, int r, uint32_t color) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x*x + y*y <= r*r)
                gui_put_pixel(cx+x, cy+y, color);
}

/* ============================================================
 * Karakter çiz (5×7 font)
 * ============================================================ */
void gui_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    int idx = c - ' ';
    if (idx < 0 || idx >= (int)(sizeof(font5x7)/sizeof(font5x7[0]))) idx = 0;
    for (int col = 0; col < 5; col++) {
        uint8_t bits = font5x7[idx][col];
        for (int row = 0; row < 7; row++) {
            uint32_t color = (bits & (1 << row)) ? fg : bg;
            gui_put_pixel(x + col, y + row, color);
        }
    }
}

void gui_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) {
        gui_draw_char(x, y, *s++, fg, bg);
        x += 6;
    }
}

/* ============================================================
 * Backbuffer → VGA memory
 * ============================================================ */
void gui_render(void) {
    for (int i = 0; i < GUI_WIDTH * GUI_HEIGHT; i++) {
        uint32_t c = backbuffer[i];
        uint8_t r = (c >> 16) & 0xFF;
        uint8_t g = (c >>  8) & 0xFF;
        uint8_t b =  c        & 0xFF;
        /* Web palette index: r/51 * 36 + g/51 * 6 + b/51 */
        uint8_t ri = r / 52; if (ri > 5) ri = 5;
        uint8_t gi = g / 52; if (gi > 5) gi = 5;
        uint8_t bi = b / 52; if (bi > 5) bi = 5;
        FRAMEBUFFER[i] = (uint8_t)(ri * 36 + gi * 6 + bi);
    }
}

/* ============================================================
 * Pencere oluştur
 * ============================================================ */
window_t *gui_create_window(const char *title, int x, int y, int w, int h) {
    if (win_count >= MAX_WINDOWS) return 0;
    window_t *win = &windows[win_count++];
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->title_color = GUI_DARK_BLUE;
    win->bg_color    = GUI_LIGHT_GREY;
    win->visible     = 1;
    win->focused     = 1;
    int i = 0;
    while (title[i] && i < 31) { win->title[i] = title[i]; i++; }
    win->title[i] = 0;
    return win;
}

void gui_draw_window(window_t *win) {
    if (!win || !win->visible) return;
    /* Gölge */
    gui_fill_rect(win->x+2, win->y+2, win->w, win->h, GUI_DARK_GREY);
    /* Arkaplan */
    gui_fill_rect(win->x, win->y, win->w, win->h, win->bg_color);
    /* Başlık çubuğu */
    gui_fill_rect(win->x, win->y, win->w, WIN_TITLE_HEIGHT, win->title_color);
    /* Başlık metni */
    gui_draw_string(win->x + 4, win->y + 5, win->title, GUI_WHITE, win->title_color);
    /* Kapat butonu */
    gui_fill_circle(win->x + win->w - 8, win->y + 9, 5, GUI_RED);
    gui_draw_string(win->x + win->w - 10, win->y + 5, "X", GUI_WHITE, GUI_RED);
    /* Kenarlık */
    gui_draw_rect(win->x, win->y, win->w, win->h, GUI_DARK_GREY);
}

/* ============================================================
 * Masaüstü çiz
 * ============================================================ */
void gui_draw_desktop(void) {
    /* Arkaplan gradyanı */
    for (int y = 0; y < GUI_HEIGHT - 16; y++) {
        uint8_t r = (uint8_t)(y / 3);
        uint8_t g = (uint8_t)(y / 5);
        uint8_t b = (uint8_t)(100 + y / 4);
        uint32_t color = (uint32_t)(0xFF000000 | ((uint32_t)r<<16) | ((uint32_t)g<<8) | b);
        for (int x = 0; x < GUI_WIDTH; x++)
            gui_put_pixel(x, y, color);
    }

    /* Görev çubuğu */
    gui_fill_rect(0, GUI_HEIGHT-16, GUI_WIDTH, 16, GUI_DARK_GREY);
    gui_draw_rect(0, GUI_HEIGHT-16, GUI_WIDTH, 16, GUI_LIGHT_GREY);

    /* Başlat butonu */
    gui_fill_rect(2, GUI_HEIGHT-14, 35, 12, GUI_DARK_BLUE);
    gui_draw_string(4, GUI_HEIGHT-12, "START", GUI_WHITE, GUI_DARK_BLUE);

    /* Saat */
    uint32_t ticks = timer_get_ticks();
    uint32_t sec   = ticks / 100;
    char time_buf[9];
    uint32_t h = (sec / 3600) % 24;
    uint32_t m = (sec / 60) % 60;
    uint32_t s = sec % 60;
    time_buf[0] = '0' + h/10; time_buf[1] = '0' + h%10;
    time_buf[2] = ':';
    time_buf[3] = '0' + m/10; time_buf[4] = '0' + m%10;
    time_buf[5] = ':';
    time_buf[6] = '0' + s/10; time_buf[7] = '0' + s%10;
    time_buf[8] = 0;
    gui_draw_string(GUI_WIDTH-50, GUI_HEIGHT-12, time_buf, GUI_WHITE, GUI_DARK_GREY);

    /* Logo */
    gui_draw_string(GUI_WIDTH/2 - 18, 4, "MyOS", GUI_YELLOW, 0);
}
