#include "../include/task.h"
#include "../include/memory.h"
#include "../include/screen.h"
#include "../include/paging.h"
#include "../include/critical.h"
#include "../include/kstring.h"
#include "../include/gdt.h"

static task_t   tasks[MAX_TASKS];
static task_t  *current_task    = 0;
static uint32_t next_pid        = 1;
static uint32_t total_ticks     = 0;
static int      multitasking_on = 0;

#define QUANTUM_BASE 5

static void task_reap(void);   /* aşağıda tanımlı; çıkış yapan görevleri toplar */

/* String yardımcıları (kstrncpy) artık include/kstring.h'de. */

/* Görev bitince buraya düşer */
static void task_exit_wrapper(void) {
    task_exit();
    while (1) __asm__ volatile ("hlt");
}

/* ============================================================
 * task_init
 * ============================================================ */
void task_init(void) {
    memset(tasks, 0, sizeof(tasks));

    task_t *idle = &tasks[0];
    idle->pid        = 0;
    idle->state      = TASK_RUNNING;
    idle->priority   = PRIORITY_NORMAL;
    idle->time_slice = QUANTUM_BASE;
    kstrncpy(idle->name, "shell", 32);
    idle->next = idle;

    current_task    = idle;
    multitasking_on = 0;

    screen_println("[TASK] Gorev yoneticisi baslatildi. PID 0 = shell");
}

void task_start_multitasking(void) {
    multitasking_on = 1;
}

/* ============================================================
 * task_create — yeni görev stack'ini şöyle kur:
 *
 *  [ task_exit_wrapper ]  ← dönüş adresi (görev bitince)
 *  [ entry             ]  ← ret ile atlanır (ilk switch)
 *  [ 0x202 (eflags)    ]  ← popfd ile yüklenir (IF=1: interrupt açık)
 *  [ 0 (ebp) ]
 *  [ 0 (ebx) ]
 *  [ 0 (esi) ]
 *  [ 0 (edi) ]   ← esp buraya ayarlanır
 *
 * context_switch: pop edi, pop esi, pop ebx, pop ebp, popfd, ret
 * ============================================================ */
task_t *task_create(const char *name, void (*entry)(void), uint8_t priority) {
    /* Önce biten görevleri topla: slot ve stack belleğini geri kazan
     * (kooperatif bağlam — heap'i güvenle değiştirebiliriz). */
    task_reap();

    /* Boş slot bul */
    task_t *t = 0;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_DEAD || tasks[i].pid == 0) {
            t = &tasks[i]; break;
        }
    }
    if (!t) { screen_println("[TASK] Gorev limiti doldu!"); return 0; }

    memset(t, 0, sizeof(task_t));
    t->pid       = next_pid++;
    t->state     = TASK_READY;
    t->priority  = priority ? priority : PRIORITY_NORMAL;
    t->time_slice = QUANTUM_BASE * t->priority;
    kstrncpy(t->name, name, 32);

    /* Stack belleği */
    uint8_t *stack_mem = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!stack_mem) { screen_println("[TASK] Stack bellek yok!"); return 0; }
    memset(stack_mem, 0, TASK_STACK_SIZE);

    t->kernel_stack = (uint32_t)(stack_mem + TASK_STACK_SIZE);

    /*
     * Stack'i yukarıdan aşağı doldur:
     * switch.asm:  pushfd; push ebp/ebx/esi/edi → kaydeder
     *              pop edi/esi/ebx/ebp; popfd    → geri yükler
     *              ret                           → stack'teki adrese atlar
     *
     * İlk çalışma için stack'i hazırla:
     */
    uint32_t *sp = (uint32_t *)t->kernel_stack;

    /* Dönüş adresi: görev bitince task_exit_wrapper çağrılır */
    *(--sp) = (uint32_t)task_exit_wrapper;
    /* İlk ret hedefi: entry fonksiyonu */
    *(--sp) = (uint32_t)entry;
    /* popfd için EFLAGS: IF=1 (0x200) + rezerve bit1 (0x2) => interrupt açık.
     * Yeni görev kendi başına interrupt'lar açık olarak başlar. */
    *(--sp) = 0x202;
    /* pop ebp, ebx, esi, edi için sıfırlar (context_switch pop sırası: edi önce) */
    *(--sp) = 0; /* ebp */
    *(--sp) = 0; /* ebx */
    *(--sp) = 0; /* esi */
    *(--sp) = 0; /* edi */

    t->context.esp = (uint32_t)sp;
    t->page_dir    = 0;

    /* Döngüsel listeye ekle — timer IRQ ring'i yarıda görmesin diye
     * pointer cerrahisini kritik bölgede yap. */
    uint32_t f = irq_save();
    t->next = current_task->next;
    current_task->next = t;
    irq_restore(f);

    screen_print("[TASK] Yeni gorev: ");
    screen_print(t->name);
    screen_print(" (PID ");
    screen_print_int((int32_t)t->pid);
    screen_println(")");
    return t;
}

/* ============================================================
 * Zamanlayıcı: sıradaki READY görevi bul
 * ============================================================ */
static task_t *scheduler_next(void) {
    task_t *t = current_task->next;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (t->state == TASK_READY) return t;
        t = t->next;
    }
    return &tasks[0];   /* hep shell'e dön */
}

/* ============================================================
 * Zombi görevleri topla: stack'lerini serbest bırak, ring'den çıkar,
 * slot'u DEAD yapıp yeniden kullanılabilir hale getir.
 *
 * current_task ASLA toplanmaz: çıkış yapan görev önce ZOMBIE olur, sonra
 * task_yield çağırır; o anda hâlâ kendi stack'i üzerindeyiz. Bu yüzden
 * toplama bir sonraki zamanlama turuna ertelenir (görev artık current
 * değilken stack'i güvenle serbest bırakılır).
 * ============================================================ */
static void task_reap(void) {
    for (int i = 1; i < MAX_TASKS; i++) {
        task_t *z = &tasks[i];
        if (z->state != TASK_ZOMBIE || z == current_task) continue;

        /* Ring'den çıkar: z'nin öncülünü bul ve bağlantıyı kritik
         * bölgede kopar (timer IRQ'su ring'i yarıda görmesin). */
        uint32_t f = irq_save();
        task_t *p = z->next;
        while (p && p->next != z) p = p->next;
        if (p) p->next = z->next;
        z->state = TASK_DEAD;
        z->next  = 0;
        irq_restore(f);

        /* Stack belleğini serbest bırak (kernel_stack = stack tepesi) */
        if (z->kernel_stack) {
            kfree((void *)(z->kernel_stack - TASK_STACK_SIZE));
            z->kernel_stack = 0;
        }
    }
}

/* ============================================================
 * task_yield — gönüllü CPU bırakma
 * ============================================================ */
void task_yield(void) {
    if (!multitasking_on || !current_task) return;

    /* Kritik bölge: seçim + current_task güncellemesi + switch timer
     * IRQ'suna karşı atomik olmalı. irq_save() interrupt'ları kapatır ve
     * çağıranın IF durumunu f'e saklar. context_switch EFLAGS'i görev
     * başına koruduğu için, her görev kendi irq_restore'unu geri
     * döndüğünde kendi stack'inde çalıştırır; yeni görevler ise IF=1'li
     * kendi frame'leriyle başlar. */
    uint32_t f = irq_save();

    task_t *prev = current_task;
    task_t *next = scheduler_next();
    if (prev == next) { irq_restore(f); return; }

    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    next->state      = TASK_RUNNING;
    next->time_slice = QUANTUM_BASE * next->priority;
    current_task     = next;

    /* TSS.esp0'i bu görevin kernel stack'ine ayarla: görev ring-3'teyse
     * IRQ/syscall/fault bu stack'e geçmeli (yoksa ring-0 görevde önemsiz). */
    if (next->kernel_stack) tss_set_kernel_stack(next->kernel_stack);

    if (next->page_dir) paging_switch(next->page_dir);

    context_switch(&prev->context, &next->context);

    irq_restore(f);
}

/* ============================================================
 * Timer tick (IRQ0)
 * ============================================================ */
void task_tick(void) {
    total_ticks++;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING) {
            if (tasks[i].sleep_ticks > 0 && --tasks[i].sleep_ticks == 0)
                tasks[i].state = TASK_READY;
        }
    }

    if (!multitasking_on || !current_task) return;
    current_task->time_used++;
    if (current_task->time_slice > 0) current_task->time_slice--;
    if (current_task->time_slice == 0) task_yield();
}

/* ============================================================
 * task_sleep
 * ============================================================ */
void task_sleep(uint32_t ms) {
    if (!current_task) return;
    uint32_t ticks = (ms + 9) / 10;
    current_task->sleep_ticks = ticks ? ticks : 1;
    current_task->state = TASK_SLEEPING;
    task_yield();
}

/* ============================================================
 * task_exit
 * ============================================================ */
void task_exit(void) {
    if (!current_task || current_task->pid == 0) return;
    task_reap();   /* önceki zombileri topla (current hariç) */
    current_task->state = TASK_ZOMBIE;
    screen_print("[TASK] Bitti: "); screen_println(current_task->name);
    task_yield();
}

/* ============================================================
 * task_kill
 * ============================================================ */
void task_kill(uint32_t pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid && tasks[i].state != TASK_DEAD) {
            tasks[i].state = TASK_ZOMBIE;
            screen_print("[TASK] Olduruldu PID ");
            screen_print_int((int32_t)pid);
            screen_putchar('\n');
            return;
        }
    }
    screen_println("[TASK] PID bulunamadi.");
}

task_t *task_current(void) { return current_task; }

/* ============================================================
 * ps — görev listesi
 * ============================================================ */
void task_print_list(void) {
    static const char *snames[] = {
        "READY","RUNNING","SLEEPING","BLOCKED","ZOMBIE","DEAD"
    };
    screen_set_color(COLOR_YELLOW, COLOR_BLACK);
    screen_println("PID  DURUM      PRI  TICK  AD");
    screen_println("---  ---------  ---  ----  --------");
    screen_set_color(COLOR_WHITE, COLOR_BLACK);
    for (int i = 0; i < MAX_TASKS; i++) {
        if ((tasks[i].pid > 0 || i == 0) && tasks[i].state != TASK_DEAD) {
            screen_print_int((int32_t)tasks[i].pid); screen_print("    ");
            screen_print(snames[tasks[i].state]);    screen_print("   ");
            screen_print_int((int32_t)tasks[i].priority); screen_print("    ");
            screen_print_int((int32_t)tasks[i].time_used); screen_print("  ");
            screen_println(tasks[i].name);
        }
    }
    screen_print("Toplam ticks: "); screen_print_int((int32_t)total_ticks); screen_putchar('\n');
}
