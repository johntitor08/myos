/* MyOS Linker Script */
/* Kernel 0x10000 adresinden başlar (bootloader buraya yükler) */

ENTRY(_start)

SECTIONS {
    . = 0x10000;

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
}
