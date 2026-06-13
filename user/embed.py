#!/usr/bin/env python3
# ELF dosyasini gomulu bir C bayt dizisine cevirir.
# Kullanim: embed.py <girdi.elf> <cikti.h>
# fs_init() bu diziyi MyFS'e "hello" dosyasi olarak yazar (run hello).
import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
with open(dst, "w") as f:
    f.write("/* OTOMATIK URETILDI - duzenlemeyin. Kaynak: user/hello.c + user/user.ld */\n")
    f.write("#ifndef HELLO_ELF_H\n#define HELLO_ELF_H\n")
    f.write("static const unsigned char hello_elf[] = {\n")
    for i in range(0, len(data), 16):
        f.write("  " + ",".join(str(b) for b in data[i:i+16]) + ",\n")
    f.write("};\n")
    f.write("static const unsigned int hello_elf_len = %d;\n" % len(data))
    f.write("#endif\n")
