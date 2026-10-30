#!/bin/bash
# Runs INSIDE a fakeroot session (state.db) so chown/chmod/mknod are recorded as root. Edits files only (no pacman).
# usage: edit-rootfs.sh <tree> <repo-dir>
#   Your SSH public key: ${TABLET_SSH_KEY:-$HOME/.ssh/omarchy-tablet_ed25519}.pub (see tools/README.md).
#   Host keys are generated fresh into the tree and copied to $HOSTKEY_DIR (default <repo>/work/rootfs/hostkeys, keep
#   private); a known_hosts line for 172.16.42.1 is written to $HOSTKEY_DIR/known_hosts.
# (Adapted from the project's poc/edit-rootfs.sh: key/overlay paths only.)
set -euo pipefail
T=$1 REPO=$2
OV=$REPO/rootfs/overlay
KEY=${TABLET_SSH_KEY:-$HOME/.ssh/omarchy-tablet_ed25519}
PUB=$(cat "$KEY.pub")

# --- hostname
echo omarchy-tablet > "$T/etc/hostname"
cat > "$T/etc/hosts" <<'E'
127.0.0.1   localhost
::1         localhost
127.0.1.1   omarchy-tablet
E

# --- network: drop the DHCP-on-everything defaults, install the static gadget config
rm -f "$T/etc/systemd/network/eth.network" "$T/etc/systemd/network/en.network"
cp "$OV/etc/systemd/network/10-usb-gadget.network" "$T/etc/systemd/network/"
chmod 644 "$T/etc/systemd/network/10-usb-gadget.network"

# --- ssh: key-only; same key for root and alarm; pre-generated host keys (stable fingerprint, no first-boot keygen)
cp "$OV/etc/ssh/sshd_config.d/10-omarchy-tablet.conf" "$T/etc/ssh/sshd_config.d/"
chmod 644 "$T/etc/ssh/sshd_config.d/10-omarchy-tablet.conf"
install -d -m 700 "$T/root/.ssh" "$T/home/alarm/.ssh"
echo "$PUB" > "$T/root/.ssh/authorized_keys";       chmod 600 "$T/root/.ssh/authorized_keys"
echo "$PUB" > "$T/home/alarm/.ssh/authorized_keys"; chmod 600 "$T/home/alarm/.ssh/authorized_keys"
chown 0:0 "$T/root/.ssh" "$T/root/.ssh/authorized_keys"
chown 1000:1000 "$T/home/alarm/.ssh" "$T/home/alarm/.ssh/authorized_keys"
HK=${HOSTKEY_DIR:-$REPO/work/rootfs/hostkeys}; mkdir -p "$HK"
for t in ed25519 ecdsa rsa; do
	rm -f "$HK/ssh_host_${t}_key" "$HK/ssh_host_${t}_key.pub"
	ssh-keygen -q -t $t -N "" -C "root@omarchy-tablet" -f "$HK/ssh_host_${t}_key"
	install -m 600 "$HK/ssh_host_${t}_key"     "$T/etc/ssh/ssh_host_${t}_key"
	install -m 644 "$HK/ssh_host_${t}_key.pub" "$T/etc/ssh/ssh_host_${t}_key.pub"
	chown 0:0 "$T/etc/ssh/ssh_host_${t}_key" "$T/etc/ssh/ssh_host_${t}_key.pub"
done
chmod 700 "$HK"; chmod 600 "$HK"/*_key
sed "s/^/172.16.42.1 /; s/ root@omarchy-tablet\$//" "$HK/ssh_host_ed25519_key.pub" > "$HK/known_hosts"

# --- journald: persistent + mirrored to kmsg
install -d -m 755 "$T/etc/systemd/journald.conf.d"
cp "$OV/etc/systemd/journald.conf.d/10-persistent.conf" "$T/etc/systemd/journald.conf.d/"
chmod 644 "$T/etc/systemd/journald.conf.d/10-persistent.conf"

# --- USB gadget keep-alive service (same script as the initramfs, sh shebang)
install -d -m 755 "$T/usr/local/sbin"
sed '1s|.*|#!/bin/sh|' "$REPO/initramfs/initramfs-src/bin/usb-gadget" > "$T/usr/local/sbin/omarchy-usb-gadget"
chmod 755 "$T/usr/local/sbin/omarchy-usb-gadget"; chown 0:0 "$T/usr/local/sbin/omarchy-usb-gadget"
cp "$OV/etc/systemd/system/omarchy-usb-gadget.service" "$T/etc/systemd/system/"
chmod 644 "$T/etc/systemd/system/omarchy-usb-gadget.service"
ln -sf /etc/systemd/system/omarchy-usb-gadget.service "$T/etc/systemd/system/multi-user.target.wants/omarchy-usb-gadget.service"
# sshd / systemd-networkd / systemd-resolved are already enabled in the ALARM tarball (verified by verify-rootfs).

# --- fstab: root by label
cat >> "$T/etc/fstab" <<'E'
LABEL=omarchy-root  /  ext4  rw,noatime,errors=remount-ro  0  1
E
: > "$T/var/log/initramfs.log"; chmod 644 "$T/var/log/initramfs.log"; chown 0:0 "$T/var/log/initramfs.log"
