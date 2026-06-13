/* MyOS userland linker scripti.
 * Programı 0x500000'e linkler (ELF yükleme penceresi: ELF_LOAD_MIN).
 * SIZEOF_HEADERS eklenerek ELF başlıkları da 0x500000'den başlayan tek
 * PT_LOAD segmentinin içinde kalır; böylece segment vaddr'i tam 0x500000
 * olur (>= ELF_LOAD_MIN) ve çekirdeğin elf_load'u kabul eder. */
ENTRY(_start)

SECTIONS {
    . = 0x500000 + SIZEOF_HEADERS;

    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.eh_frame_hdr) }
}
