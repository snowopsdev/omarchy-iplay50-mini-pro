#!/bin/sh
# Read-only. Coordinator runs while an existing justified tone is active.
# No playback, routing, gain, GPIO, PMIC, jack-reader or power changes.
set -u
printf 'boot='; cat /proc/sys/kernel/random/boot_id
printf 'uptime='; cat /proc/uptime
printf '\n## PCM0\n'
cat /proc/asound/card0/pcm0p/sub0/status /proc/asound/card0/pcm0p/sub0/hw_params
printf '\n## Route readbacks\n'
for name in 'Lineout Volume' 'Headset Volume' 'Handset Volume' 'DAC In Mux' 'LOL Mux' 'HPL Mux' 'HPR Mux' 'RCV Mux' 'Ext_Speaker_Amp Switch' 'ADDA_DL_CH1 DL1_CH1' 'ADDA_DL_CH2 DL1_CH2'; do
    amixer -c 0 cget "name=$name" 2>&1
done
printf '\n## GPIO49 and neighboring pins; headers preserve column meanings\n'
for file in /sys/kernel/debug/pinctrl/*/pins /sys/kernel/debug/pinctrl/*/pinconf-pins /sys/kernel/debug/pinctrl/*/pinmux-pins; do
    [ -r "$file" ] || continue
    printf '# %s\n' "$file"
    head -n 5 "$file"
    grep -E '^pin (49|50|51|52|53) ' "$file" || true
done
printf '\n## AFE pinctrl state\n'
grep -n -B 1 -A 5 -E 'afe-pcm|mt6789-afe|sound' /sys/kernel/debug/pinctrl/pinctrl-handles 2>/dev/null || true
printf '\n## Relevant DAPM widget files (first 16 lines each)\n'
find /sys/kernel/debug/asoc -type f -path '*/dapm/*' 2>/dev/null | while IFS= read -r file; do
    case "$file" in
        *Ext_Speaker_Amp*|*LINEOUT*|*Lineout*|*DAC*|*ADDA*|*Playback*|*Headphone*)
            printf '# %s\n' "$file"; head -n 16 "$file";;
    esac
done
printf '\n## Recent amp/codec events\n'
dmesg | grep -iE 'spk_amp_event|extamp|ext_amp|aw87.*(fail|error)|lineout|adda_dl_event|pinctrl.*(49|afe)' | tail -n 45
printf '\n## Live DT amp properties (hex; no assumption about firmware image match)\n'
find -L /proc/device-tree -type f 2>/dev/null | while IFS= read -r file; do
    case "$file" in
        */sound/extamp_mode|*/aud_gpio_extamp_on/*/pinmux|*/aud_gpio_extamp_off/*/pinmux|*/aud_gpio_extamp_on/*/output-high|*/aud_gpio_extamp_off/*/output-low)
            printf '# %s\n' "$file"; od -An -tx1 "$file";;
    esac
done
