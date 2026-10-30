#!/usr/bin/env python3
"""Read-only installation receipt; contains no browser history or environment."""
import hashlib,json,pathlib,socket,subprocess
P=pathlib.Path
units=['omarchy-tablet-vcu.service','omarchy-usb-host-current.service','omarchy-usb-gadget.service','omarchy-desktop.service']
files=['/opt/omarchy-tablet/video/lib/mt6789-v4l2-bridge.so','/opt/omarchy-tablet/video/lib/chromium-gl-finish-loader.so','/opt/omarchy-tablet/video/bin/chromium-gpu-launcher','/opt/omarchy-tablet/video/modules/mtk-vcodec-dec-v1-dmabuf-only.ko','/opt/omarchy-tablet/diagnostics/usb-host-current.py','/etc/systemd/system/omarchy-usb-host-current.service','/opt/omarchy-tablet/gpu/panvk-fourfect/lib/libvulkan_panfrost.so','/home/hypr/.config/hypr/looknfeel.lua','/home/hypr/.config/chromium-flags.conf']
def run(args):
    return subprocess.run(args,capture_output=True,text=True).stdout.strip()
processes=[]
for p in P('/proc').iterdir():
    if not p.name.isdecimal(): continue
    try:
        raw=(p/'cmdline').read_bytes().decode(errors='replace')
        args=raw.replace('\0',' ').split()
        if not args or not args[0].endswith('/chromium'): continue
        processes.append({'pid':int(p.name),'type':next((x for x in args if x.startswith('--type=')),'browser'),'debugging_arg':any(x.startswith('--remote-debugging-') for x in args),'bridge_loaded':'mt6789-v4l2-bridge.so' in (p/'maps').read_text(errors='replace')})
    except (FileNotFoundError,PermissionError,ProcessLookupError): pass
s=socket.socket(); debug=s.connect_ex(('127.0.0.1',9223))==0; s.close()
report={'boot_id':P('/proc/sys/kernel/random/boot_id').read_text().strip(),'services':{u:{'active':run(['systemctl','is-active',u]),'enabled':run(['systemctl','is-enabled',u])} for u in units},'readiness':json.loads(P('/run/omarchy-tablet-video-ready').read_text()),'usb_current':{'policy':P('/sys/devices/platform/charger/input_current').read_text().strip(),'master':P('/sys/class/power_supply/mtk-master-charger/input_current_limit').read_text().strip(),'chip':P('/sys/class/power_supply/5-006a/input_current_limit').read_text().strip()},'usb_states':{p.parent.name:p.read_text().strip() for p in P('/sys/class/udc').glob('*/state')},'failed_system_units':run(['systemctl','--failed','--no-legend']),'files_sha256':{p:hashlib.sha256(P(p).read_bytes()).hexdigest() for p in files},'chromium_processes':processes,'debug_port_9223_listening':debug,'normal_flags_contain_debugging':'remote-debugging' in P('/home/hypr/.config/chromium-flags.conf').read_text()}
print(json.dumps(report,indent=2))
