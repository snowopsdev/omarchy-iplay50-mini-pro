#!/bin/bash
# Desktop-side helper for the tablet's USB gadget link (CDC-NCM, or ECM fallback).
#   ./host-usbnet.sh            find the interface, configure 172.16.42.2/24 if it can, else print what to run
#   ./host-usbnet.sh --wait     wait up to 60 s for the interface to appear first
#   ./host-usbnet.sh --install  print the one-time (sudo) systemd-networkd drop-in that makes this automatic forever
# The tablet side is 172.16.42.1/24. The MACs are fixed in the initramfs (usb-gadget):
#   NCM: tablet 02:a1:b2:c3:d4:01  host 02:a1:b2:c3:d4:02      ECM: tablet 02:a1:b2:c3:d4:11  host 02:a1:b2:c3:d4:12
set -u
HOST_IP=172.16.42.2/24 TAB_IP=172.16.42.1
HERE=$(cd "$(dirname "$0")" && pwd)
MACS="02:a1:b2:c3:d4:02 02:a1:b2:c3:d4:12"

find_if() {
	local n m a
	for n in /sys/class/net/*; do
		a=$(cat "$n/address" 2>/dev/null) || continue
		for m in $MACS; do [ "$a" = "$m" ] && { basename "$n"; return 0; }; done
	done
	return 1
}

if [ "${1:-}" = "--install" ]; then
	cat <<E
One-time setup (needs root; afterwards the link configures itself whenever the tablet is plugged in):
  sudo install -m 644 $HERE/host/05-omarchy-tablet.network /etc/systemd/network/05-omarchy-tablet.network
  sudo networkctl reload
E
	exit 0
fi

if [ "${1:-}" = "--wait" ]; then
	for _ in $(seq 60); do find_if >/dev/null && break; sleep 1; done
fi

IF=$(find_if) || {
	echo "No interface with MAC $MACS yet. Is the tablet plugged in and past the initramfs USB step?"
	echo "Candidates (USB ethernet):"; for n in /sys/class/net/*; do
		d=$(readlink -f "$n/device" 2>/dev/null); case "$d" in */usb*) echo "  $(basename $n) $(cat $n/address)";; esac
	done
	echo "Also check:  lsusb | grep -i -E '1d6b:0104|Linux Foundation'   and   dmesg | tail"
	exit 1
}
echo "tablet link interface: $IF ($(cat /sys/class/net/$IF/address), driver $(basename "$(readlink -f /sys/class/net/$IF/device/driver)" 2>/dev/null))"

if ip -4 -o addr show dev "$IF" | grep -q " ${HOST_IP%/*}/"; then
	echo "already configured: $HOST_IP on $IF"
else
	done_=0
	# unprivileged route 1: NetworkManager (polkit normally lets an active local user manage system connections).
	# The profile is matched by the fixed host MAC, so it also applies to future plug-ins of the same tablet.
	if command -v nmcli >/dev/null && nmcli -t -f RUNNING general 2>/dev/null | grep -q running; then
		MAC=$(cat /sys/class/net/$IF/address)
		CON=omarchy-tablet-$(echo "$MAC" | tr -d ':')
		nmcli connection delete "$CON" >/dev/null 2>&1
		if nmcli connection add type ethernet ifname "*" con-name "$CON" 802-3-ethernet.mac-address "$MAC" \
			ipv4.method manual ipv4.addresses "$HOST_IP" ipv4.never-default yes ipv6.method disabled \
			connection.autoconnect yes >/dev/null 2>&1 \
			&& nmcli connection up "$CON" ifname "$IF" >/dev/null 2>&1; then
			echo "configured $HOST_IP on $IF via NetworkManager (connection '$CON', persistent)"; done_=1
		fi
	fi
	# unprivileged route 2: nothing else works without root (ip addr add needs CAP_NET_ADMIN)
	if [ $done_ = 0 ]; then
		echo "Root is needed to assign the address. Run exactly:"
		echo "  sudo ip addr add $HOST_IP dev $IF && sudo ip link set $IF up"
		echo "or, if you use systemd-networkd instead of NetworkManager, make it permanent (one time):"
		echo "  $0 --install"
		exit 2
	fi
fi
echo -n "ping $TAB_IP: "; ping -c1 -W2 $TAB_IP >/dev/null 2>&1 && echo ok || echo "no reply yet (try: telnet $TAB_IP  /  ssh omarchy-tablet)"
