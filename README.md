# MyOS - x86 İşletim Sistemi

Sıfırdan yazılmış, x86 mimarisi için minimal bir işletim sistemi.

## Mimari

```
myos/
├── boot/
│   └── boot.asm        # Stage 1 Bootloader (512 byte, BIOS uyumlu)
├── kernel/
│   ├── kernel_entry.asm # 32-bit giriş noktası
│   ├── kernel.c         # Ana kernel fonksiyonu
│   ├── idt.c            # Interrupt Descriptor Table
│   ├── isr.asm          # ISR/IRQ stub'ları
│   ├── memory.c         # Heap yöneticisi (kmalloc/kfree)
│   └── linker.ld        # Linker scripti
├── drivers/
│   ├── screen.c         # VGA text mode sürücüsü
│   └── keyboard.c       # PS/2 klavye sürücüsü
├── fs/
│   └── fs.c             # In-memory dosya sistemi (MyFS)
├── shell/
│   └── shell.c          # Komut satırı arayüzü
└── include/             # Header dosyaları
```

## Özellikler

- **Bootloader**: 16-bit Real Mode → 32-bit Protected Mode geçişi
- **Kernel**: C ile yazılmış monolitik kernel
- **IDT**: CPU exception handler'ları + IRQ desteği
- **VGA Sürücüsü**: 80x25 renkli text mode
- **Bellek Yöneticisi**: First-fit heap, kmalloc/kfree/kcalloc/krealloc
- **Klavye**: PS/2 klavye IRQ handler, scan code çevirisi
- **Dosya Sistemi**: In-memory MyFS (64 dosya, 256 block)
- **Shell**: Komut satırı (help, ls, cat, echo, calc, write, del, meminfo...)

## Derleme

### Gereksinimler

```bash
# Ubuntu/Debian
sudo apt-get install nasm gcc gcc-multilib binutils qemu-system-x86

# Arch Linux
sudo pacman -S nasm gcc qemu
```

### Derle ve Çalıştır

```bash
cd myos
make        # Derle
make run    # QEMU'da çalıştır
make debug  # GDB ile debug et
make clean  # Temizle
```

## Shell Komutları

| Komut | Açıklama |
|-------|----------|
| `help` | Yardım menüsü |
| `ls` | Dosya listesi |
| `cat <dosya>` | Dosya içeriği |
| `write <dosya>` | Dosyaya yaz |
| `del <dosya>` | Dosya sil |
| `echo <metin>` | Metin yazdır |
| `calc 5 + 3` | Hesap makinesi |
| `meminfo` | Bellek istatistikleri |
| `sync` | Dosya sistemini ATA diskine kaydet (kalıcı) |
| `mount` | Dosya sistemini diskten yükle |
| `uname` | Sistem bilgisi |
| `clear` | Ekranı temizle |
| `reboot` | Yeniden başlat |

> **Kalıcı depolama:** `sync` ile FS ATA diskine yazılır; boot sırasında
> diskte geçerli bir MyFS varsa otomatik yüklenir (`mount`), böylece
> dosyalar yeniden başlatmalar arasında korunur. QEMU'da bir disk imajıyla
> deneyin: `qemu-system-i386 -fda myos.img -hda disk.img` (örn. önce
> `qemu-img create disk.img 16M`).

## Nasıl Çalışıyor?

1. **BIOS**, boot sektörünü (ilk 512 byte) `0x7C00` adresine yükler
2. **Bootloader** (`boot.asm`): disk'ten kernel'i okur, Protected Mode'a geçer
3. **Kernel Entry** (`kernel_entry.asm`): `kernel_main()` fonksiyonunu çağırır
4. **Kernel** (`kernel.c`): IDT, bellek, klavye, FS başlatır; shell'i çağırır
5. **Shell** kullanıcı komutlarını alır, işler

## Geliştirme Yol Haritası

- [x] Sanal bellek / sayfalama (paging)
- [x] Süreç yönetimi (multitasking)
- [x] Sistem çağrıları (syscall)
- [x] ATA disk sürücüsü (gerçek disk I/O)
- [x] Kalıcı dosya sistemi (MyFS'i ATA diskine sync/mount)
- [x] ELF binary yükleme
- [ ] Kullanıcı alanı (ring 3) — altyapı var, kabuk entegrasyonu bekliyor
- [ ] FAT16 dosya sistemi
