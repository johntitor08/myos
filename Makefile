# MyOS Build Sistemi
# Gereksinimler: nasm, gcc (cross-compiler veya i686-elf-gcc), qemu

# Araçlar
ASM     = nasm
CC      = gcc
LD      = ld

# Flags
ASMFLAGS  = -f elf32
CFLAGS    = -m32 -ffreestanding -fno-stack-protector -fno-pic \
            -nostdlib -nostdinc -Wall -Wextra -O2 \
            -I./include
LDFLAGS   = -m elf_i386 -T kernel/linker.ld --oformat binary

# Kaynak dosyaları
# ÖNEMLI: kernel_entry.asm ilk sırada olmalı; flat binary'de _start
# (call kernel_main) 0x10000'e gelmeli çünkü bootloader oraya atlar.
ASM_SRCS  = kernel/kernel_entry.asm kernel/isr.asm kernel/switch.asm
C_SRCS    = kernel/kernel.c kernel/idt.c kernel/memory.c kernel/paging.c \
            kernel/elf.c kernel/syscall.c kernel/task.c \
            drivers/screen.c drivers/keyboard.c drivers/timer.c drivers/ata.c \
            fs/fs.c shell/shell.c net/net.c gui/gui.c

# Object dosyaları
ASM_OBJS  = $(ASM_SRCS:.asm=.o)
C_OBJS    = $(C_SRCS:.c=.o)

# Hedefler
.PHONY: all clean run debug

all: myos.img

# Bootloader derle
# KERNEL_SECTORS, kernel.bin boyutundan üretilip nasm'a verilir; böylece
# bootloader tam olarak kernel kadar sektör okur ve boyut arttıkça
# manuel güncelleme gerekmez.
boot/boot.bin: boot/boot.asm kernel.bin
	@SECTORS=$$(( ( $$(stat -c%s kernel.bin) + 511 ) / 512 )); \
	echo "Bootloader: kernel = $$SECTORS sektor (KERNEL_SECTORS)"; \
	$(ASM) -f bin -dKERNEL_SECTORS=$$SECTORS $< -o $@

# ASM kernel dosyaları
%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

# C kernel dosyaları
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel binary
# $^ prereq sırasını korur: önce ASM_OBJS (kernel_entry.o ilk), sonra C_OBJS.
kernel.bin: $(ASM_OBJS) $(C_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "kernel.bin boyutu:"
	@ls -l kernel.bin | awk '{print $$5 " byte (" int(($$5+511)/512) " sektor)"}'

# Disk imajı oluştur (bootloader + kernel)
myos.img: boot/boot.bin kernel.bin
	@echo "Disk imaji olusturuluyor..."
	dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null
	dd if=boot/boot.bin of=$@ conv=notrunc 2>/dev/null
	dd if=kernel.bin of=$@ seek=1 conv=notrunc 2>/dev/null
	@echo "Tamamlandi: myos.img"
	@ls -lh myos.img

# QEMU ile calistir
run: myos.img
	qemu-system-i386 -fda myos.img -boot a

# Debug modunda calistir
debug: myos.img
	qemu-system-i386 -fda myos.img -boot a -s -S &
	gdb -ex "target remote localhost:1234" \
	    -ex "symbol-file kernel.bin"

# Temizle
clean:
	rm -f boot/boot.bin kernel.bin myos.img
	rm -f $(ASM_OBJS) $(C_OBJS)
	@echo "Temizlendi."
