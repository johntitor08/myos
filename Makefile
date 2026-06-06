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
ASM_SRCS  = kernel/kernel_entry.asm kernel/isr.asm
C_SRCS    = kernel/kernel.c kernel/idt.c kernel/memory.c \
            drivers/screen.c drivers/keyboard.c \
            fs/fs.c shell/shell.c

# Object dosyaları
ASM_OBJS  = $(ASM_SRCS:.asm=.o)
C_OBJS    = $(C_SRCS:.c=.o)

# Hedefler
.PHONY: all clean run

all: myos.img

# Bootloader derle
boot/boot.bin: boot/boot.asm
	$(ASM) -f bin $< -o $@

# ASM kernel dosyaları
%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

# C kernel dosyaları
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel binary
kernel.bin: $(ASM_OBJS) $(C_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

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
