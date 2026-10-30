#!/bin/sh
# READ-ONLY: pack the LIVE device tree (base + applied dtbo overlay) so the host can decompile it with `dtc -I fs`.
# Usage: sh dump-dt.sh   -> /tmp/live-dt.tgz  (copy it back to the host)
tar -C /sys/firmware/devicetree/base -czf /tmp/live-dt.tgz . && ls -l /tmp/live-dt.tgz
# On the host:  mkdir d && tar -C d -xzf live-dt.tgz && dtc -I fs -O dts -o live.dts d
