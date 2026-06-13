# MyOS Build Sistemi
# Gereksinimler: nasm, gcc (cross-compiler veya i686-elf-gcc), qemu

# Araçlar
ASM     = nasm
CC      = gcc
LD      = ld
HOSTCC  = cc          # host (native) derleyici - testler icin

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
            kernel/elf.c kernel/syscall.c kernel/task.c kernel/gdt.c \
            drivers/screen.c drivers/keyboard.c drivers/timer.c drivers/ata.c \
            fs/fs.c shell/shell.c net/net.c gui/gui.c

# Object dosyaları
ASM_OBJS  = $(ASM_SRCS:.asm=.o)
C_OBJS    = $(C_SRCS:.c=.o)

# Hedefler
.PHONY: all clean run debug test

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

# Host-tarafı regresyon testleri (QEMU gerektirmez)
# - boot sektörü tam 512 bayt mı?
# - LBA->CHS yükleyici kerneli birebir yeniden kuruyor mu?
# - RTL8139 RX ofseti halka içinde sarıyor mu?
test: myos.img tests/boot_loader_sim.c tests/rtl8139_rx_sim.c tests/fs_persist_sim.c tests/gdt_sim.c
	@echo "== Boot sektoru boyutu =="
	@SZ=$$(stat -c%s boot/boot.bin); \
	 if [ "$$SZ" -ne 512 ]; then echo "HATA: boot.bin $$SZ bayt (512 olmali)"; exit 1; fi; \
	 echo "OK: boot.bin = 512 bayt"
	@echo "== Yukleyici (LBA->CHS) simulasyonu =="
	@$(HOSTCC) -O2 -Wall -Wextra tests/boot_loader_sim.c -o tests/boot_loader_sim
	@./tests/boot_loader_sim myos.img kernel.bin
	@echo "== RTL8139 RX ofset simulasyonu =="
	@$(HOSTCC) -O2 -Wall -Wextra tests/rtl8139_rx_sim.c -o tests/rtl8139_rx_sim
	@./tests/rtl8139_rx_sim
	@echo "== Kalici FS (sync/mount) uctan uca testi =="
	@# -nostdinc -Iinclude: kernel'in stdint.h/stddef.h'ini kullan (fs.h
	@#  <stdint.h> ister); libc yine printf/memcpy icin baglanir.
	@# -fno-builtin: kernel mem*'lerini GCC builtin'leriyle kiyaslayip
	@#  uyari uretmesin (kernel size_t 32-bit, host builtin 64-bit bekler).
	@$(HOSTCC) -O2 -Wall -Wextra -nostdinc -Iinclude -fno-builtin \
	    tests/fs_persist_sim.c -o tests/fs_persist_sim
	@./tests/fs_persist_sim
	@echo "== GDT/TSS descriptor kodlama testi =="
	@$(HOSTCC) -O2 -Wall -Wextra -nostdinc -Iinclude -fno-builtin \
	    tests/gdt_sim.c -o tests/gdt_sim
	@./tests/gdt_sim
	@echo "Tum testler gecti."

# Temizle
clean:
	rm -f boot/boot.bin kernel.bin myos.img
	rm -f $(ASM_OBJS) $(C_OBJS)
	rm -f tests/boot_loader_sim tests/rtl8139_rx_sim tests/fs_persist_sim tests/gdt_sim
	@echo "Temizlendi."
