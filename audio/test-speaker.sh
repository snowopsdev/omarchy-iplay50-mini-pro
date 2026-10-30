#!/bin/sh
# Test the configured desktop speaker through PipeWire. No mixer/GPIO changes.
set -eu
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(id -u)" = 0 ]; then
    cd /home/hypr
    exec runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1001/bus "$DIR/test-speaker.sh" "$@"
fi
[ "${HEADPHONE:-0}" = 0 ] || { echo 'This profile currently supports the built-in speaker; headset validation is separate.' >&2; exit 2; }
[ -z "${PCM:-}" ] || { echo 'Use the desktop PipeWire output; raw PCM overrides are unsupported here.' >&2; exit 2; }
WAV=${1:-$DIR/tone-440-m30dBFS-L-R-LR.wav}
[ -r "$WAV" ] || { echo "Missing tone: $WAV" >&2; exit 2; }
exec timeout --kill-after=2s 20s pw-play "$WAV"
