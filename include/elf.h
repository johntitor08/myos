#ifndef ELF_H
#define ELF_H

#include "stdint.h"

/* ELF Magic */
#define ELF_MAGIC   0x464C457F   /* "\x7FELF" little-endian */

/* ELF türleri */
#define ET_EXEC     2
#define EM_386      3

/* Program header tipleri */
#define PT_LOAD     1

/* ELF32 File Header */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  bits;          /* 1=32bit, 2=64bit */
    uint8_t  endian;        /* 1=little */
    uint8_t  hdr_version;
    uint8_t  os_abi;
    uint8_t  padding[8];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;         /* Entry point */
    uint32_t ph_offset;     /* Program header offset */
    uint32_t sh_offset;     /* Section header offset */
    uint32_t flags;
    uint16_t hdr_size;
    uint16_t ph_entry_size;
    uint16_t ph_count;
    uint16_t sh_entry_size;
    uint16_t sh_count;
    uint16_t sh_str_index;
} elf32_header_t;

/* ELF32 Program Header */
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t offset;     /* Dosyadaki offset */
    uint32_t vaddr;      /* Sanal adres */
    uint32_t paddr;      /* Fiziksel adres */
    uint32_t file_size;  /* Dosyadaki boyut */
    uint32_t mem_size;   /* Bellekteki boyut */
    uint32_t flags;
    uint32_t align;
} elf32_phdr_t;

/* Yüklenen program bilgisi */
typedef struct {
    uint32_t entry;      /* Giriş noktası */
    uint32_t stack;      /* User stack */
    int      valid;
} elf_program_t;

int elf_load(const uint8_t *data, uint32_t size, elf_program_t *out);
int elf_validate(const uint8_t *data, uint32_t size);

#endif
