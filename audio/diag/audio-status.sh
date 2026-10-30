#!/bin/sh
# Read-only audio snapshot for the real tablet. Run as root to include debugfs.
# Does not activate UCM verbs, change mixers, start playback, or restart services.
set -u
TABLET_USER=${TABLET_USER:-hypr}
TABLET_UID=$(id -u "$TABLET_USER") || exit 1
as_user() {
    runuser -u "$TABLET_USER" -- env \
        XDG_RUNTIME_DIR="/run/user/$TABLET_UID" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$TABLET_UID/bus" "$@"
}
section() { printf '\n## %s\n' "$1"; }
section identity
date -u
id "$TABLET_USER"
uname -r
section packages
pacman -Q pipewire pipewire-audio pipewire-alsa pipewire-pulse wireplumber alsa-lib alsa-ucm-conf alsa-utils 2>&1
section permissions
ls -l /dev/snd/controlC* /dev/snd/pcmC*D0p 2>/dev/null
as_user test -r /dev/snd/controlC0
printf 'desktop can read controlC0: exit=%s\n' "$?"
for name in pipewire pipewire-pulse wireplumber; do
    for pid in $(pgrep -u "$TABLET_UID" -x "$name"); do
        printf '%s pid=%s ' "$name" "$pid"
        grep '^Groups:' "/proc/$pid/status"
    done
done
section kernel-card
cat /proc/asound/cards /proc/asound/pcm
for file in /proc/asound/card0/pcm0p/sub0/status /proc/asound/card0/pcm0p/sub0/hw_params; do
    printf '%s\n' "$file"
    cat "$file" 2>/dev/null
done
section pipewire
as_user wpctl status
as_user pactl list cards
as_user pactl list sinks short
section mixers
for name in 'Lineout Volume' 'Headset Volume' 'DAC In Mux' 'LOL Mux' 'HPL Mux' 'HPR Mux' 'Ext_Speaker_Amp Switch' 'ADDA_DL_CH1 DL1_CH1' 'ADDA_DL_CH2 DL1_CH2'; do
    amixer -c 0 cget "name=$name" 2>&1
done
section recent-audio-log
journalctl -b "_UID=$TABLET_UID" --no-pager -n 1000 | \
    rg -i 'alsa|ucm|snd|audio|wireplumber|pipewire' | tail -n 100
