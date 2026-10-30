#!/omarchy/bin/busybox sh
# wifi-up-reload.sh - wifi-up.sh with the Wi-Fi modules forcibly unloaded and re-inserted after the loader stage (see wifi-up.sh header).
RELOAD=1; export RELOAD; exec /omarchy/bin/busybox sh "$(dirname "$0")/wifi-up.sh" "$@"
