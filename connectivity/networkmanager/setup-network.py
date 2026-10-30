#!/usr/bin/env python3
"""Reuse the existing Wi-Fi secret locally; never print it or pass it in argv."""
from pathlib import Path
import json
import re
import uuid

source = Path('/etc/wpa_supplicant/wpa_supplicant-wlan0.conf').read_text()
blocks = re.findall(r'network\s*=\s*\{(.*?)\}', source, re.S)
if len(blocks) != 1:
    raise SystemExit('Expected one saved Wi-Fi network; leave networking unchanged')

def value(key):
    match = re.search(r'^\s*' + key + r'\s*=\s*("(?:\\.|[^"\\])*"|[^\s#]+)',
                      blocks[0], re.M)
    if not match:
        raise SystemExit('Saved Wi-Fi profile lacks a required field')
    raw = match.group(1)
    return json.loads(raw) if raw.startswith('"') else raw

ssid = value('ssid')
if not re.search(r'^\s*ssid\s*=\s*"', blocks[0], re.M):
    ssid = bytes.fromhex(ssid).decode()
psk = value('psk')
if any(c in ssid + psk for c in '\n\r'):
    raise SystemExit('Profile needs keyfile escaping; leave networking unchanged')

directory = Path('/etc/NetworkManager/system-connections')
directory.mkdir(parents=True, exist_ok=True)
profile = directory / 'tablet-wifi.nmconnection'
profile.write_text(f'''[connection]
id=Tablet Wi-Fi
uuid={uuid.uuid4()}
type=wifi
interface-name=wlan0
autoconnect=true

[wifi]
mode=infrastructure
ssid={ssid}
cloned-mac-address=permanent

[wifi-security]
key-mgmt=wpa-psk
psk={psk}
# The vendor driver connects reliably with WPA2; automatic SAE/PMF negotiation
# advertised by NetworkManager failed during association on this device.
pmf=1
auth-alg=open
proto=rsn;

[ipv4]
method=auto

[ipv6]
method=auto
''')
profile.chmod(0o600)
Path('/etc/NetworkManager/conf.d/20-tablet.conf').write_text('''# Preserve the USB debugging link and vendor virtual interfaces.
[main]
dns=systemd-resolved

[keyfile]
unmanaged-devices=interface-name:usb0;interface-name:wlan1;interface-name:p2p0;interface-name:ap0;interface-name:dummy0;interface-name:ifb0;interface-name:ifb1
''')
Path('/etc/systemd/network/05-wlan0-unmanaged.network').write_text('''[Match]
Name=wlan0

[Link]
Unmanaged=yes
''')
print('Saved existing Wi-Fi credentials for NetworkManager; USB remains unmanaged')
