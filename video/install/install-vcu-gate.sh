#!/bin/bash
set -euo pipefail
test ! -e /opt/omarchy-tablet/video/browser-enabled
systemctl stop omarchy-tablet-vcu.service
vcu_backup=/opt/omarchy-tablet/backups/vcu-browser-gate-$(date -u +%Y%m%dT%H%M%SZ)
install -d -m0700 "$vcu_backup"
cp -a /opt/omarchy-tablet/video/runtime/supervisor.py "$vcu_backup/supervisor.py"
cp -a /etc/systemd/system/omarchy-tablet-vcu.service "$vcu_backup/omarchy-tablet-vcu.service"
install -m0755 /root/vcu-service-candidate/browser-access.py /opt/omarchy-tablet/video/runtime/browser-access.py
install -m0755 /root/vcu-service-candidate/supervisor.py /opt/omarchy-tablet/video/runtime/supervisor.py
install -m0644 /root/vcu-service-candidate/omarchy-tablet-vcu.service /etc/systemd/system/omarchy-tablet-vcu.service
/opt/omarchy-tablet/video/runtime/browser-access.py --lock
systemd-analyze verify /etc/systemd/system/omarchy-tablet-vcu.service
systemctl daemon-reload
systemctl start omarchy-tablet-vcu.service
systemctl is-active omarchy-tablet-vcu.service
stat -c '%U:%G %a %n' /dev/video0 /dev/dma_heap/mtk_mm
test ! -e /run/omarchy-tablet-video-ready
printf 'BACKUP=%s\n' "$vcu_backup"
