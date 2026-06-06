#include "../include/keyboard.h"
#include "../include/idt.h"
#include "../include/screen.h"

/* Klavye tamponu */
#define KB_BUFFER_SIZE 256
static char kb_buffer[KB_BUFFER_SIZE];
static int  kb_read  = 0;
static int  kb_write = 0;
static int  kb_count = 0;

/* Shift durumu */
static int shift_pressed = 0;
static int caps_lock = 0;

/* US QWERTY scan code tablosu */
static const char scancode_table[] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' '
};

static const char scancode_shift[] = {
    0, 0, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' '
};

/* ============================================================
 * Port I/O
 * ============================================================ */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ============================================================
 * IRQ1 handler - klavye interrupt'ı
 * ============================================================ */
static void keyboard_handler(registers_t *regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    /* Key release (0x80+ scan code) */
    if (scancode & 0x80) {
        uint8_t key = scancode & 0x7F;
        if (key == 0x2A || key == 0x36) shift_pressed = 0; /* Shift bırakıldı */
        return;
    }

    /* Özel tuşlar */
    switch (scancode) {
        case 0x2A: case 0x36: shift_pressed = 1; return;  /* Shift */
        case 0x3A: caps_lock = !caps_lock; return;          /* Caps Lock */
        case 0x1C: /* Enter */
            if (kb_count < KB_BUFFER_SIZE) {
                kb_buffer[kb_write] = '\n';
                kb_write = (kb_write + 1) % KB_BUFFER_SIZE;
                kb_count++;
                screen_putchar('\n');
            }
            return;
        case 0x0E: /* Backspace */
            if (kb_count < KB_BUFFER_SIZE) {
                kb_buffer[kb_write] = '\b';
                kb_write = (kb_write + 1) % KB_BUFFER_SIZE;
                kb_count++;
                screen_putchar('\b');
            }
            return;
    }

    /* Normal karakter */
    if (scancode < sizeof(scancode_table)) {
        char c;
        int use_shift = shift_pressed;
        if (caps_lock && scancode >= 0x10 && scancode <= 0x32) use_shift = !use_shift;

        c = use_shift ? scancode_shift[scancode] : scancode_table[scancode];
        if (c && kb_count < KB_BUFFER_SIZE) {
            kb_buffer[kb_write] = c;
            kb_write = (kb_write + 1) % KB_BUFFER_SIZE;
            kb_count++;
            screen_putchar(c);
        }
    }
}

/* ============================================================
 * Klavyeyi başlat
 * ============================================================ */
void keyboard_init(void) {
    register_irq_handler(1, keyboard_handler);
}

/* ============================================================
 * Tampondan karakter oku (blocking)
 * ============================================================ */
char keyboard_getchar(void) {
    while (kb_count == 0) {
        __asm__ volatile ("hlt");
    }
    char c = kb_buffer[kb_read];
    kb_read = (kb_read + 1) % KB_BUFFER_SIZE;
    kb_count--;
    return c;
}

/* ============================================================
 * Satır oku (Enter'a kadar)
 * ============================================================ */
int keyboard_readline(char *buf, int max_len) {
    int i = 0;
    while (i < max_len - 1) {
        char c = keyboard_getchar();
        if (c == '\n') break;
        if (c == '\b') {
            if (i > 0) i--;
        } else {
            buf[i++] = c;
        }
    }
    buf[i] = '\0';
    return i;
}
