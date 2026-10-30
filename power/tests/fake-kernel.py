#!/usr/bin/env python3
"""Emulates mtk_battery_netlink_handler + fg_daemon_send_data of mt6358_battery.ko as read from its disassembly
(ANALYSIS.md section 2), over an AF_UNIX abstract datagram socket, with a fake sysfs tree.
It checks the exact things the real kernel checks (magic, cmd>1 requires registered pid, fg_cmd_check, version compare)
and rejects what the kernel would reject.  It cannot prove the real driver behaves the same - that is what the on-device test is for."""
import os, socket, struct, sys, threading

MAGIC = 2015060303
HDR = 32
root, name = sys.argv[1], sys.argv[2]
mode = sys.argv[3] if len(sys.argv) > 3 else "normal"   # normal | match (daemon sends matching versions => stays daemon mode)
psy = os.path.join(root, name)
state = {"fgd_pid": 0, "algo": False, "log": []}

def setattr_(a, v):
    with open(os.path.join(psy, a), "w") as f:
        f.write(f"{v}\n")

srv = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
srv.bind("\0" + os.environ.get("FGD_TEST_SOCK", "fgd-test-kernel"))
print("READY", flush=True)
while True:
    data, addr = srv.recvfrom(65536)
    nlh = struct.unpack_from("<IHHII", data, 0)
    seq, sender_pid = nlh[3], nlh[4]          # a real kernel takes the pid from skb creds; the test client puts it in nlmsg_pid
    f = struct.unpack_from("<8I", data, 16)
    nl_cmd, cmd, h, sub, para1, dlen, rlen, ident = f
    payload = data[16 + HDR:]
    def log(s):
        state["log"].append(s); print("K:", s, flush=True)
    if ident != MAGIC:
        log(f"not correct MTKFG netlink packet {ident}"); continue
    if state["fgd_pid"] != sender_pid and cmd > 1:
        log(f"drop rev netlink pid:{sender_pid}:{state['fgd_pid']} cmd:{cmd}"); continue
    if len(data) < 16 + HDR + dlen:
        log("short payload"); continue
    ret = bytearray(HDR + rlen)                # kernel kmalloc(ret_len + 32)
    struct.pack_into("<2I", ret, 0, nl_cmd, cmd)
    reply_len = HDR
    if cmd == 1:
        if sub == 0 and para1 != HDR:
            log("fuel gauge version error (msleep 10 s)"); 
        state["fgd_pid"] = struct.unpack_from("<I", payload, 0)[0]
        log(f"FG_DAEMON_CMD_SET_DAEMON_PID = {state['fgd_pid']}(first launch)")
    elif cmd == 5:
        tot, size, idx = struct.unpack_from("<3I", payload, 0)
        if tot != 20: log(f"vc size is different {tot}")
        if idx + size > 20: log("size is different"); continue
        if rlen < 12: log("KERNEL OVERFLOW: ret buffer smaller than 12 bytes"); sys.exit(9)
        struct.pack_into("<3I", ret, HDR, tot, size, idx)
        vc = struct.unpack_from("<5i", payload, 12)
        log(f"VersionControl {vc}")
        if mode == "match":
            vc = (12, 118, 17, 776, 39020)
        if vc[1:] != (118, 17, 776, 39020):
            state["algo"] = True
            log("enable Kernel mode Gauge; MTK Battery algo init bat_exist:1")
            setattr_("init_done", 1); setattr_("capacity", 63)
    else:
        log(f"bad cmd {cmd}")
    struct.pack_into("<8I", ret, 0, nl_cmd, cmd, h, 0, 0, 0, 0, MAGIC)   # fgd_data_len = 0 in the reply header
    out = struct.pack("<IHHII", 16 + reply_len, 0, 0, seq, 0) + bytes(ret[:reply_len])
    srv.sendto(out, addr)
