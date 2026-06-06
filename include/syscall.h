#ifndef SYSCALL_H
#define SYSCALL_H

#include "stdint.h"
#include "idt.h"

/* Sistem çağrısı numaraları */
#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_READ    2
#define SYS_OPEN    3
#define SYS_CLOSE   4
#define SYS_GETPID  5
#define SYS_SLEEP   6
#define SYS_FORK    7
#define SYS_EXEC    8
#define SYS_SBRK    9
#define SYS_UPTIME  10

#define SYSCALL_INT 0x80
#define MAX_SYSCALLS 16

/* Sistem çağrısı handler tipi */
typedef uint32_t (*syscall_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t);

void     syscall_init(void);
uint32_t syscall_dispatch(registers_t *regs);

#endif
