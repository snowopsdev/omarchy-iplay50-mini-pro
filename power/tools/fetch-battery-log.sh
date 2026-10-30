#!/bin/bash
# Copy the tablet's battery logs to power/log/ (git-ignored) and print the cycle report.
# Uses the shared SSH helper tools/tablet (root on the tablet).
REPO=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$REPO/power/log"
"$REPO/tools/tablet" 'tar -C /var/lib/tablet-battery-log -cf - .' | tar -C "$REPO/power/log" -xf -
python3 "$REPO/power/tools/battery-cycle-report.py" "$REPO/power/log"
