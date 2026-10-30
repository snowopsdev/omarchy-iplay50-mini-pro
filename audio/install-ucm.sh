#!/bin/sh
# Run ON THE TABLET as root:  sudo sh install-ucm.sh [/usr/share/alsa/ucm2]
# Copies the MediaTek/mt6789-mt6366 tree and creates conf.d/<driver>/<name>.conf links that match what
# alsa-lib will look up for this card (driver + longname/name are read from `alsactl info`).
set -e
SRC="$(cd "$(dirname "$0")" && pwd)/ucm2"; DST="${1:-/usr/share/alsa/ucm2}"; CARD="${CARD:-0}"
mkdir -p "$DST/MediaTek"; cp -a "$SRC/MediaTek/mt6789-mt6366" "$DST/MediaTek/"
info="$(alsactl info "$CARD")"
get() { echo "$info" | sed -n "s/^ *$1: *//p" | head -n1 | sed "s/^'//; s/'$//"; }
DRIVER="$(get driver_name)"; NAME="$(get name)"; LONG="$(get longname)"
[ -n "$DRIVER" ] || { echo 'Cannot determine ALSA driver_name' >&2; exit 1; }
echo "driver='$DRIVER' name='$NAME' longname='$LONG'"
for n in "$LONG" "$NAME" "mt6789-mt6366"; do
    [ -n "$n" ] || continue
    mkdir -p "$DST/conf.d/$DRIVER"
    ln -sf "../../MediaTek/mt6789-mt6366/mt6789-mt6366.conf" "$DST/conf.d/$DRIVER/$n.conf"
    echo "linked $DST/conf.d/$DRIVER/$n.conf"
done
if [ -f "$(dirname "$SRC")/tablet-speaker.conf" ]; then
    install -d /etc/alsa/conf.d
    install -m 644 "$(dirname "$SRC")/tablet-speaker.conf" /etc/alsa/conf.d/99-tablet-speaker.conf
fi
if [ -f "$(dirname "$SRC")/51-tablet-speaker.conf" ]; then
    install -d -o hypr -g hypr /home/hypr/.config/wireplumber/wireplumber.conf.d
    install -m 644 -o hypr -g hypr "$(dirname "$SRC")/51-tablet-speaker.conf" /home/hypr/.config/wireplumber/wireplumber.conf.d/51-tablet-speaker.conf
fi
echo "--- check:"; alsaucm -c "hw:$CARD" list _verbs && alsaucm -c "hw:$CARD" set _verb HiFi list _devices
echo "Then: systemctl --user restart wireplumber pipewire pipewire-pulse"
