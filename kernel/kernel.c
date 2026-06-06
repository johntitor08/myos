#include "../include/screen.h"
#include "../include/idt.h"
#include "../include/memory.h"
#include "../include/keyboard.h"
#include "../include/fs.h"
#include "../include/shell.h"
#include "../include/paging.h"
#include "../include/timer.h"
#include "../include/task.h"
#include "../include/ata.h"
#include "../include/net.h"
#include "../include/syscall.h"
#include "stdint.h"

static void task_heartbeat(void) {
    const char spin[] = "|/-\\";
    int i = 0;
    while (1) {
        volatile uint16_t *vga = (uint16_t *)0xB8000;
        vga[79] = (uint16_t)spin[i%4] | (0x0A << 8);
        i++;
        task_sleep(250);
    }
}

static void task_net_poll(void) {
    while (1) {
        net_receive();
        task_sleep(50);
    }
}

void kernel_main(void) {
    screen_init();
    screen_clear();

    screen_set_color(COLOR_CYAN, COLOR_BLACK);
    screen_println("##############################################");
    screen_println("#        MyOS v3.0  -  Kernel Basliyor      #");
    screen_println("#  ATA+ELF+Ring3+Syscall+Net+GUI+Multitask  #");
    screen_println("##############################################");
    screen_set_color(COLOR_WHITE, COLOR_BLACK);

    screen_print("[1/9] IDT...        "); idt_init(); __asm__ volatile("sti");
    screen_set_color(COLOR_LIGHT_GREEN,COLOR_BLACK); screen_println("[ OK ]"); screen_set_color(COLOR_WHITE,COLOR_BLACK);

    screen_print("[2/9] Bellek...     "); memory_init();
    screen_set_color(COLOR_LIGHT_GREEN,COLOR_BLACK); screen_println("[ OK ]"); screen_set_color(COLOR_WHITE,COLOR_BLACK);
    memory_print_stats();

    screen_print("[3/9] Paging...     "); paging_init();
    screen_set_color(COLOR_LIGHT_GREEN,COLOR_BLACK); screen_println("[ OK ]"); screen_set_color(COLOR_WHITE,COLOR_BLACK);

    screen_print("[4/9] Timer...      "); timer_init();
    screen_set_color(COLOR_LIGHT_GREEN,COLOR_BLACK); screen_println("[ OK ]"); screen_set_color(COLOR_WHITE,COLOR_BLACK);

    screen_print("[5/9] Gorevler...   "); task_init();
    screen_set_color(COLOR_LIGHT_GREEN,COLOR_BLACK); screen_println("[ OK ]"); screen_set_color(COLOR_WHITE,COLOR_BLACK);

    screen_print("[6/9] Klavye...     "); keyboard_init();
    screen_set_color(COLOR_LIGHT_GREEN,COLOR_BLACK); screen_println("[ OK ]"); screen_set_color(COLOR_WHITE,COLOR_BLACK);

    screen_print("[7/9] Dosya Sistemi..."); fs_init();
    screen_set_color(COLOR_LIGHT_GREEN,COLOR_BLACK); screen_println("[ OK ]"); screen_set_color(COLOR_WHITE,COLOR_BLACK);

    screen_print("[8/9] ATA Disk...   ");
    if (ata_init() == 0) {
        screen_set_color(COLOR_LIGHT_GREEN,COLOR_BLACK); screen_println("[ OK ]");
        screen_set_color(COLOR_WHITE,COLOR_BLACK); ata_print_info();
    } else {
        screen_set_color(COLOR_YELLOW,COLOR_BLACK); screen_println("[SKIP]");
        screen_set_color(COLOR_WHITE,COLOR_BLACK);
    }

    screen_print("[9/9] Ag (RTL8139)...");
    if (net_init() == 0) {
        screen_set_color(COLOR_LIGHT_GREEN,COLOR_BLACK); screen_println("[ OK ]");
        screen_set_color(COLOR_WHITE,COLOR_BLACK); net_print_info();
        task_create("net_poll", task_net_poll, PRIORITY_LOW);
    } else {
        screen_set_color(COLOR_YELLOW,COLOR_BLACK); screen_println("[SKIP]");
        screen_set_color(COLOR_WHITE,COLOR_BLACK);
    }

    /* Sistem çağrıları */
    syscall_init();

    /* Arka plan görevleri */
    task_create("heartbeat", task_heartbeat, PRIORITY_LOW);

    screen_set_color(COLOR_CYAN,COLOR_BLACK);
    screen_println("##############################################");
    screen_println("#         MyOS v3.0 hazir! Shell...         #");
    screen_println("##############################################");
    screen_set_color(COLOR_WHITE,COLOR_BLACK);

    task_start_multitasking();

    /* Boot bitti: 1.5 saniye goster sonra temizle */
    timer_wait(1500);
    screen_clear();

    /* Mini durum satiri */
    screen_set_color(COLOR_CYAN, COLOR_BLACK);
    screen_println("MyOS v3.0 | help yazin | gui=grafik mod | ps=surecler");
    screen_set_color(COLOR_DARK_GREY, COLOR_BLACK);
    screen_println("------------------------------------------------");
    screen_set_color(COLOR_WHITE, COLOR_BLACK);
    shell_run();

    while (1) __asm__ volatile("hlt");
}
