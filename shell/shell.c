#include "../include/shell.h"
#include "../include/screen.h"
#include "../include/keyboard.h"
#include "../include/memory.h"
#include "../include/fs.h"
#include "../include/task.h"
#include "../include/timer.h"
#include "../include/paging.h"
#include "../include/ata.h"
#include "../include/net.h"
#include "../include/gui.h"
#include "../include/elf.h"
#include "../include/kstring.h"
#include "stdint.h"

#define CMD_MAX  256
#define ARG_MAX  8

/* kstrcmp/kstrncpy/katoi artık include/kstring.h'de. */

static int parse_args(char *cmd, char *args[], int max) {
    int argc=0; char *p=cmd;
    while(*p&&argc<max){
        while(*p==' ')p++;
        if(!*p)break;
        args[argc++]=p;
        while(*p&&*p!=' ')p++;
        if(*p)*p++='\0';
    }
    return argc;
}

static void cmd_help(void) {
    screen_set_color(COLOR_YELLOW,COLOR_BLACK);
    screen_println("MyOS v3.0 Shell:");
    screen_set_color(COLOR_WHITE,COLOR_BLACK);
    screen_println(" Dosya: ls, cat, write, del, cp, mv, hexdump");
    screen_println(" Sistem: ps, kill, sleep, meminfo, uname, uptime, clear, reboot");
    screen_println(" Hesap: calc <a> <op> <b>     (op: + - * / %)");
    screen_println(" Disk: diskinfo, diskread <lba>");
    screen_println(" Ag: netinfo, ping <ip>, udpsend <ip> <port> <msg>");
    screen_println(" GUI: gui");
    screen_println(" Diger: echo, help");
}

static void cmd_uname(void) {
    screen_set_color(COLOR_CYAN,COLOR_BLACK);
    screen_println("MyOS v3.0 | x86 32-bit | Paging+MT+Syscall+ATA+Net+GUI");
    screen_set_color(COLOR_WHITE,COLOR_BLACK);
}

static void cmd_uptime(void) {
    uint32_t t=timer_get_ticks(), s=t/100, m=s/60, h=m/60;
    screen_print("Uptime: "); screen_print_int((int32_t)h); screen_print("h ");
    screen_print_int((int32_t)(m%60)); screen_print("m ");
    screen_print_int((int32_t)(s%60)); screen_print("s (");
    screen_print_int((int32_t)t); screen_println(" tick)");
}

static void cmd_calc(char **av, int ac) {
    if(ac<4){screen_println("Kullanim: calc <sayi> <op> <sayi>");return;}
    int32_t a=katoi(av[1]),b=katoi(av[3]),r=0;
    char op=av[2][0];
    if(op=='+')r=a+b; else if(op=='-')r=a-b;
    else if(op=='*')r=a*b;
    else if(op=='/'||op=='%'){
        if(!b){screen_println("Sifira bolme!");return;}
        r = op=='/'?a/b:a%b;
    } else {screen_println("Gecersiz op!"); return;}
    screen_print_int(a);screen_putchar(op);screen_print_int(b);
    screen_print("="); screen_set_color(COLOR_LIGHT_GREEN,COLOR_BLACK);
    screen_print_int(r); screen_set_color(COLOR_WHITE,COLOR_BLACK); screen_putchar('\n');
}

static void cmd_write(char *fn) {
    if(!fn){screen_println("Kullanim: write <dosya>");return;}
    static char buf[4096]; static char line[256]; int tot=0;
    screen_println("Yaz (bos satir = bitti):");
    while(1){
        screen_print("> ");
        int len=keyboard_readline(line,256);
        if(!len)break;
        if(tot+len+1<4096){kstrncpy(buf+tot,line,4096-tot);tot+=len;buf[tot++]='\n';}
    }
    buf[tot]=0;
    if(fs_write(fn,buf,(uint32_t)tot)>=0){
        screen_print("Yazildi: ");screen_print(fn);
        screen_print(" (");screen_print_int(tot);screen_println("B)");
    }
}

static void print_hex2(uint8_t b) {
    const char *h = "0123456789ABCDEF";
    char s[3]; s[0]=h[b>>4]; s[1]=h[b&0xF]; s[2]=0;
    screen_print(s);
}

static void cmd_cp(char **av, int ac) {
    if(ac<3){screen_println("Kullanim: cp <kaynak> <hedef>");return;}
    static char cb[8192];
    int n=fs_read(av[1],cb,sizeof(cb));
    if(n<0){screen_println("Kaynak bulunamadi!");return;}
    if(fs_write(av[2],cb,(uint32_t)n)>=0){
        screen_print("Kopyalandi: ");screen_print(av[1]);
        screen_print(" -> ");screen_println(av[2]);
    } else screen_println("Yazma hatasi!");
}

static void cmd_mv(char **av, int ac) {
    if(ac<3){screen_println("Kullanim: mv <kaynak> <hedef>");return;}
    static char mb[8192];
    int n=fs_read(av[1],mb,sizeof(mb));
    if(n<0){screen_println("Kaynak bulunamadi!");return;}
    if(fs_write(av[2],mb,(uint32_t)n)<0){screen_println("Yazma hatasi!");return;}
    fs_delete(av[1]);
    screen_print("Tasindi: ");screen_print(av[1]);
    screen_print(" -> ");screen_println(av[2]);
}

static void cmd_hexdump(char *fn) {
    if(!fn){screen_println("Kullanim: hexdump <dosya>");return;}
    static char hb[8192];
    int n=fs_read(fn,hb,sizeof(hb));
    if(n<0){screen_println("Dosya bulunamadi!");return;}
    for(int off=0; off<n; off+=16){
        screen_print_hex((uint32_t)off); screen_print(": ");
        for(int i=0;i<16;i++){
            if(off+i<n){ print_hex2((uint8_t)hb[off+i]); screen_putchar(' '); }
            else screen_print("   ");
        }
        screen_print(" ");
        for(int i=0;i<16 && off+i<n;i++){
            char c=hb[off+i];
            screen_putchar((c>=32 && c<127)?c:'.');
        }
        screen_putchar('\n');
    }
    screen_print("Toplam: "); screen_print_int(n); screen_println(" byte");
}

static void cmd_diskread(uint32_t lba) {
    static uint8_t sec[512];
    if(ata_read_sectors(lba,1,sec)!=0){screen_println("Okuma hatasi!");return;}
    screen_print("LBA "); screen_print_int((int32_t)lba); screen_println(":");
    for(int i=0;i<64;i++){
        screen_print_hex(sec[i]); screen_putchar(' ');
        if((i+1)%16==0) screen_putchar('\n');
    }
}

static void cmd_udpsend(char **av, int ac) {
    if(ac<4){screen_println("Kullanim: udpsend <ip> <port> <msg>");return;}
    /* IP parse: a.b.c.d */
    uint8_t ip[4]={0}; int n=0; char *p=av[1];
    for(int i=0;i<4;i++){
        while(*p>='0'&&*p<='9'){ip[i]=(uint8_t)(ip[i]*10+(*p-'0'));p++;}
        if(*p=='.')p++;
    }
    ip_addr_t dst = (uint32_t)ip[0]|((uint32_t)ip[1]<<8)|((uint32_t)ip[2]<<16)|((uint32_t)ip[3]<<24);
    uint16_t port=(uint16_t)katoi(av[2]);
    char *msg=av[3]; int mlen=0; while(msg[mlen])mlen++;
    if(udp_send(dst,12345,port,msg,(uint16_t)mlen)==0){
        screen_print("Gonderildi: "); screen_println(msg);
    } else screen_println("Hata: Ag karti yok!");
    (void)n;
}

static void cmd_ping(char **av, int ac) {
    if(ac<2){screen_println("Kullanim: ping <ip>");return;}
    uint8_t ip[4]={0}; char *p=av[1];
    for(int i=0;i<4;i++){
        while(*p>='0'&&*p<='9'){ip[i]=(uint8_t)(ip[i]*10+(*p-'0'));p++;}
        if(*p=='.')p++;
    }
    ip_addr_t dst=(uint32_t)ip[0]|((uint32_t)ip[1]<<8)|((uint32_t)ip[2]<<16)|((uint32_t)ip[3]<<24);
    int ok=0;
    for(uint16_t seq=1; seq<=4; seq++){
        net_ping_clear();
        net_send_ping(dst, seq);
        int got=0;
        for(int t=0;t<100;t++){            /* ~1s timeout (100 * 10ms) */
            if(net_ping_check(seq)){got=1;break;}
            task_sleep(10);
        }
        if(got){ ok++; screen_print("Yanit "); screen_print(av[1]);
                 screen_print(" seq="); screen_print_int(seq); screen_putchar('\n'); }
        else   { screen_print("Zaman asimi seq="); screen_print_int(seq); screen_putchar('\n'); }
    }
    screen_print("ping: "); screen_print_int(ok); screen_println("/4 yanit");
}

/* GUI demo görevi */
static void gui_demo_task(void) {
    gui_init();

    /* Masaüstü */
    gui_draw_desktop();

    /* Pencereler */
    window_t *w1 = gui_create_window("Dosya Yoneticisi", 10, 20, 100, 80);
    window_t *w2 = gui_create_window("Terminal", 120, 20, 100, 60);
    window_t *w3 = gui_create_window("Hesap Makinesi", 50, 110, 90, 60);

    /* Pencere oluşturulamazsa (limit doldu) NULL deref'ten kaçın */
    if (!w1 || !w2 || !w3) {
        screen_println("[GUI] Pencere olusturulamadi.");
        return;
    }

    gui_draw_window(w1);
    gui_draw_window(w2);
    gui_draw_window(w3);

    /* İçerik */
    gui_draw_string(w1->x+4, w1->y+22, "readme.txt", GUI_BLACK, GUI_LIGHT_GREY);
    gui_draw_string(w1->x+4, w1->y+32, "hello.txt",  GUI_BLACK, GUI_LIGHT_GREY);
    gui_draw_string(w2->x+4, w2->y+22, "myos> _",    GUI_GREEN, GUI_DARK_GREY);
    gui_fill_rect(w2->x+4, w2->y+20, w2->w-8, w2->h-24, GUI_BLACK);
    gui_draw_string(w2->x+6, w2->y+22, "myos> help", 0xFF00FF00, GUI_BLACK);

    /* Şekiller */
    gui_fill_circle(w3->x+20, w3->y+45, 8, GUI_LIGHT_GREY);
    gui_draw_string(w3->x+16, w3->y+42, "7", GUI_BLACK, GUI_LIGHT_GREY);
    gui_fill_circle(w3->x+40, w3->y+45, 8, GUI_LIGHT_GREY);
    gui_draw_string(w3->x+36, w3->y+42, "8", GUI_BLACK, GUI_LIGHT_GREY);
    gui_fill_circle(w3->x+60, w3->y+45, 8, GUI_LIGHT_GREY);
    gui_draw_string(w3->x+56, w3->y+42, "9", GUI_BLACK, GUI_LIGHT_GREY);

    gui_render();

    /* Animasyon döngüsü */
    int frame = 0;
    while (1) {
        /* Saati güncelle */
        gui_draw_desktop();
        gui_draw_window(w1); gui_draw_window(w2); gui_draw_window(w3);

        /* Dönen nesne */
        int cx = 270, cy = 80;
        gui_fill_circle(cx, cy, 20, GUI_DARK_BLUE);
        int px = cx + (int)(15 * (frame%10 < 5 ? 1 : -1));
        int py = cy + (frame % 20) - 10;
        gui_fill_circle(px, py, 4, GUI_YELLOW);
        gui_draw_string(cx-12, cy-4, "MyOS", GUI_WHITE, GUI_DARK_BLUE);

        gui_render();
        frame++;
        task_sleep(50);
    }
}

static void cmd_reboot(void) {
    screen_set_color(COLOR_RED,COLOR_BLACK);
    screen_println("Yeniden baslatiliyor...");
    uint8_t t; __asm__ volatile("cli");
    do { __asm__ volatile("inb $0x64,%0":"=a"(t));
         if(t&1) __asm__ volatile("inb $0x60,%0":"=a"(t));
    } while(t&2);
    __asm__ volatile("outb %0,$0x64"::"a"((uint8_t)0xFE));
    __asm__ volatile("hlt");
}

void shell_run(void) {
    static char cmd_buf[CMD_MAX];
    char *argv[ARG_MAX]; int argc;

    screen_set_color(COLOR_GREEN,COLOR_BLACK);
    screen_println("Shell hazir. 'help' yazin.");
    screen_set_color(COLOR_WHITE,COLOR_BLACK);

    while(1){
        screen_set_color(COLOR_LIGHT_GREEN,COLOR_BLACK); screen_print("myos");
        screen_set_color(COLOR_WHITE,COLOR_BLACK); screen_print("> ");
        keyboard_readline(cmd_buf,CMD_MAX);
        if(!cmd_buf[0])continue;
        argc=parse_args(cmd_buf,argv,ARG_MAX);
        if(!argc)continue;

        if     (!kstrcmp(argv[0],"help"))   cmd_help();
        else if(!kstrcmp(argv[0],"clear"))  screen_clear();
        else if(!kstrcmp(argv[0],"uname"))  cmd_uname();
        else if(!kstrcmp(argv[0],"uptime")) cmd_uptime();
        else if(!kstrcmp(argv[0],"ls"))     fs_list();
        else if(!kstrcmp(argv[0],"meminfo"))memory_print_stats();
        else if(!kstrcmp(argv[0],"ps"))     task_print_list();
        else if(!kstrcmp(argv[0],"reboot")) cmd_reboot();
        else if(!kstrcmp(argv[0],"calc"))   cmd_calc(argv,argc);
        else if(!kstrcmp(argv[0],"write"))  cmd_write(argc>=2?argv[1]:0);
        else if(!kstrcmp(argv[0],"diskinfo")) ata_print_info();
        else if(!kstrcmp(argv[0],"netinfo"))  net_print_info();
        else if(!kstrcmp(argv[0],"diskread")){
            if(argc>=2) cmd_diskread((uint32_t)katoi(argv[1]));
            else screen_println("Kullanim: diskread <lba>");
        }
        else if(!kstrcmp(argv[0],"udpsend")) cmd_udpsend(argv,argc);
        else if(!kstrcmp(argv[0],"ping"))    cmd_ping(argv,argc);
        else if(!kstrcmp(argv[0],"gui")) {
            screen_println("GUI moduna geciliyor... (Ctrl+Alt+G -> QEMU)");
            task_create("gui_demo", gui_demo_task, PRIORITY_NORMAL);
        }
        else if(!kstrcmp(argv[0],"cat")){
            if(argc<2){screen_println("Kullanim: cat <dosya>");continue;}
            static char fb[4096]; int n=fs_read(argv[1],fb,4095);
            if(n<0)screen_println("Dosya bulunamadi!");
            else{fb[n]=0;screen_print(fb);if(n>0&&fb[n-1]!='\n')screen_putchar('\n');}
        }
        else if(!kstrcmp(argv[0],"del")){
            if(argc<2){screen_println("Kullanim: del <dosya>");continue;}
            if(!fs_delete(argv[1])){screen_print("Silindi: ");screen_println(argv[1]);}
            else screen_println("Bulunamadi!");
        }
        else if(!kstrcmp(argv[0],"cp"))      cmd_cp(argv,argc);
        else if(!kstrcmp(argv[0],"mv"))      cmd_mv(argv,argc);
        else if(!kstrcmp(argv[0],"hexdump")) cmd_hexdump(argc>=2?argv[1]:0);
        else if(!kstrcmp(argv[0],"echo")){
            for(int i=1;i<argc;i++){if(i>1)screen_putchar(' ');screen_print(argv[i]);}
            screen_putchar('\n');
        }
        else if(!kstrcmp(argv[0],"kill")){
            if(argc<2){screen_println("Kullanim: kill <pid>");continue;}
            task_kill((uint32_t)katoi(argv[1]));
        }
        else if(!kstrcmp(argv[0],"sleep")){
            if(argc<2){screen_println("Kullanim: sleep <ms>");continue;}
            screen_print("Bekleniyor...\n");
            task_sleep((uint32_t)katoi(argv[1]));
            screen_println("Tamam.");
        }
        else {
            screen_set_color(COLOR_LIGHT_RED,COLOR_BLACK);
            screen_print("Bilinmeyen: "); screen_println(argv[0]);
            screen_set_color(COLOR_WHITE,COLOR_BLACK);
        }
    }
}
