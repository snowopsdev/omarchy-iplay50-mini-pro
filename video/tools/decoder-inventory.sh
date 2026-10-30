#!/bin/sh
# Read-only inventory. Does not load modules, open codec devices, or start daemons.
set -eu

printf 'Kernel: '
uname -r
printf '\nVideo device nodes and sysfs names\n'
for node in /dev/video* /dev/media* /dev/vpud* /dev/vcu* /dev/vcp*; do
    [ -e "$node" ] && ls -l "$node"
done
for name in /sys/class/video4linux/*/name; do
    [ -r "$name" ] || continue
    printf '%s: ' "$name"
    cat "$name"
done
printf '\nLoaded codec modules\n'
sed -n '/vcodec\|vcu\|vcp\|vdec/p' /proc/modules
printf '\nInstalled module metadata\n'
for module in mtk_vcodec_dec_v1 mtk_vcu mtk_vcodec_common vcp vdec_fmt; do
    modinfo "$module" 2>/dev/null | sed -n '/^filename:/p; /^depends:/p; /^firmware:/p; /^vermagic:/p' || true
done
printf '\nVendor VCU daemon metadata (not executed)\n'
for daemon in /vendor/bin/v3avpud /vendor/bin/vpud; do
    [ -f "$daemon" ] || continue
    file "$daemon" || true
    readelf -l "$daemon" | sed -n '/interpreter/p' || true
    readelf -d "$daemon" | sed -n '/NEEDED/p' || true
done
for init in /vendor/etc/init/v3avpud.rc /vendor/etc/init/vpud.rc; do
    [ -f "$init" ] || continue
    printf '\n%s\n' "$init"
    cat "$init"
done
printf '\nKernel codec initialization messages\n'
dmesg | sed -n '/[Vv][Cc][Uu]\|vcodec\|vdec\|vpud/p' | tail -100
