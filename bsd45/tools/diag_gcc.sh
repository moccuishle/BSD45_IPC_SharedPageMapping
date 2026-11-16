#!/usr/bin/env bash
cd /c/Users/Administrator/Desktop/work/bsd45 || exit 1
gcc=/c/msys64/ucrt64/bin/gcc.exe
$gcc -m32 -std=gnu89 -ffreestanding -nostdinc \
  -I build/inc -I build/gen -I kernel -I kernel/arch -I kernel/fs \
  -I kernel/driver -I kernel/net -DKERNEL -E -o /tmp/um.i kernel/other/usermode.c
echo "preprocess exit=$?"
ls -l /tmp/um.i 2>/dev/null
$gcc -m32 -std=gnu89 -ffreestanding -nostdinc \
  -I build/inc -I build/gen -I kernel -I kernel/arch -I kernel/fs \
  -I kernel/driver -I kernel/net -DKERNEL -fsyntax-only kernel/other/usermode.c
echo "syntax exit=$?"
