#!/bin/sh
# READ-ONLY: what does the jack input device "mt6789-mt6366 Headset Jack" report? Plug/unplug while it runs.
# Usage: sh probe-jack.sh        (add `sudo` if /dev/input/event* is not readable; optional: pacman -S evtest)
NAME="${JACK_NAME:-mt6789-mt6366 Headset Jack}"
IN=""; for d in /sys/class/input/input*; do [ "$(cat "$d/name" 2>/dev/null)" = "$NAME" ] && IN="$d"; done
[ -n "$IN" ] || { echo "no input device named '$NAME'"; grep -H . /sys/class/input/input*/name; exit 1; }
EV="$(ls -d "$IN"/event* | head -n1 | xargs basename)"
echo "input: $IN  event node: /dev/input/$EV"
for k in ev key sw; do printf 'capabilities/%s = ' "$k"; cat "$IN/capabilities/$k"; done
SW="$(cat "$IN/capabilities/sw")"
# SW_* numbers (linux/input-event-codes.h): LID 0, TABLET_MODE 1, HEADPHONE_INSERT 2, RFKILL_ALL 3, MICROPHONE_INSERT 4,
# DOCK 5, LINEOUT_INSERT 6, JACK_PHYSICAL_INSERT 7, VIDEOOUT_INSERT 8, LINEIN_INSERT 13
# (bitmap printed as hex words, least-significant word last)
LOW="${SW##* }"; LOW=$((0x$LOW))
for pair in 2:SW_HEADPHONE_INSERT 4:SW_MICROPHONE_INSERT 6:SW_LINEOUT_INSERT 7:SW_JACK_PHYSICAL_INSERT 8:SW_VIDEOOUT_INSERT; do
    b="${pair%%:*}"; printf '  %-26s %s\n' "${pair#*:}" "$([ $(( (LOW >> b) & 1 )) = 1 ] && echo reported || echo no)"
done
if command -v evtest >/dev/null; then
    for s in SW_HEADPHONE_INSERT SW_MICROPHONE_INSERT; do
        evtest --query "/dev/input/$EV" EV_SW "$s"; echo "current $s: exit=$? (10 = inserted, 0 = not)"; done
else echo "evtest not installed: current state not queried"; fi
udevadm info -q property -p "$IN" 2>/dev/null | grep -E 'ID_INPUT|NAME'
