# gps/

The vendor GPS daemon `mnld` running natively in a private Bionic chroot,
bridged to `gpsd`. Guide: [docs/13-gps.md](../docs/13-gps.md).

| File | Installed as | Purpose |
|---|---|---|
| `gpsprops.c`, `build-gpsprops.sh` | `/opt/omarchy-tablet/gps/shim/lib64/libgpsprops.so` | LD_PRELOAD shim answering Android property reads (chip IDs, driver-ready flag) |
| `ld.config.txt` | `/opt/omarchy-tablet/gps/ld.config.txt` | Private Bionic linker config: vendor + VNDK v31 namespace linked to a system namespace |
| `gps-runtime-inner.sh` | `/opt/omarchy-tablet/gps/` | Builds the chroot (in fresh mount/PID namespaces) and execs `mnld` |
| `logd-sink.py` | `/opt/omarchy-tablet/gps/` | Receives Android log datagrams so `mnld`'s log lands in `/run/omarchy-tablet-gps/mnld.log` |
| `tablet-gnss-bridge.py` | `/opt/omarchy-tablet/gps/` | Speaks `mnld`'s HAL socket protocol; NMEA out on the pty `/run/tablet-gps/nmea` |
| `mnl-probe.py` | `/opt/omarchy-tablet/gps/` | Diagnostic: start GPS for N seconds and print every reply type |
| `tablet-gps-mnld.service`, `tablet-gps.service` | `/etc/systemd/system/` | Daemon unit and bridge unit (GPS runs while `tablet-gps` runs) |
