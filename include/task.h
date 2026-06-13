#ifndef TASK_H
#define TASK_H

#include "stdint.h"
#include "paging.h"

typedef enum {
    TASK_READY    = 0,
    TASK_RUNNING  = 1,
    TASK_SLEEPING = 2,
    TASK_BLOCKED  = 3,
    TASK_ZOMBIE   = 4,
    TASK_DEAD     = 5,
} task_state_t;

#define PRIORITY_LOW    1
#define PRIORITY_NORMAL 2
#define PRIORITY_HIGH   3
#define PRIORITY_RT     4

#define TASK_STACK_SIZE 8192
#define MAX_TASKS       16

/* context_switch sadece esp kaydeder/yükler.
   Diğer register'lar push/pop ile stack üzerinden taşınır. */
typedef struct {
    uint32_t esp;           /* Kaydedilen kernel stack pointer */
} cpu_context_t;

typedef struct task {
    uint32_t         pid;
    char             name[32];
    task_state_t     state;
    uint8_t          priority;
    cpu_context_t    context;
    page_directory_t *page_dir;
    uint32_t         kernel_stack;   /* Stack'in en üst adresi */
    uint32_t         sleep_ticks;
    uint32_t         time_used;
    uint32_t         time_slice;
    struct task     *next;
} task_t;

void    task_init(void);
void    task_start_multitasking(void);
task_t *task_create(const char *name, void (*entry)(void), uint8_t priority);
void    task_yield(void);
void    task_sleep(uint32_t ms);
void    task_exit(void);
void    task_kill(uint32_t pid);
task_t *task_current(void);
int     task_alive(uint32_t pid);
void    task_tick(void);
void    task_print_list(void);

/* ASM: context_switch(&old_esp_field, &new_esp_field) */
extern void context_switch(cpu_context_t *old, cpu_context_t *new_ctx);

#endif
