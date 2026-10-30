# tools/ — desktop-side access to the tablet

All device work in this project went over a USB cable: the tablet presents itself as a USB network
adapter (a configfs gadget) from the very first seconds of boot, and later as the same adapter
under systemd. Nothing here needs adb.

| File | What it does |
|---|---|
| `tablet` | `ssh` as root to the tablet with the project's options. Other scripts call it. |
| `ssh-config.snippet` | The same settings as an `~/.ssh/config` block (`ssh omarchy-tablet`). |
| `host-usbnet.sh` | Finds the tablet's USB network interface on the desktop and gives it 172.16.42.2/24 (through NetworkManager without root, or prints the `sudo` command). |
| `host/05-omarchy-tablet.network` | The same thing permanently, for desktops that use systemd-networkd (`host-usbnet.sh --install` prints how). |
| `tsh` | Runs one command in the **initramfs rescue shell** (telnet, port 23) and returns its output and exit code. |
| `tsh-put` | Copies a file into the rescue shell (netcat; needs `socat` on the desktop) and checks its sha256. Called `tput` in the project. |
| `usb-proxy.py` | Small HTTP/HTTPS forward proxy that listens on 172.16.42.2:3128 only, for giving the tablet internet over the cable before Wi-Fi works. |

## The USB link

- Tablet side: CDC-NCM gadget (falls back to CDC-ECM if the host enumerates it but never brings the
  link up), USB ID `1d6b:0104`, on the `musb-hdrc` controller. Address **172.16.42.1/24**.
- Fixed MAC addresses, so the desktop can match on them: NCM tablet `02:a1:b2:c3:d4:01` / desktop
  `02:a1:b2:c3:d4:02`; ECM tablet `02:a1:b2:c3:d4:11` / desktop `02:a1:b2:c3:d4:12`.
- Desktop side: **172.16.42.2/24**.
- Two phases, same addresses:
  1. **initramfs** (`initramfs/initramfs-src/bin/usb-gadget watch`): telnet on port 23, root, **no
     password**; fallback `nc 172.16.42.1 2323`. Only reachable over the cable.
  2. **Arch** (`omarchy-usb-gadget.service` + `10-usb-gadget.network` from `rootfs/overlay`): sshd,
     key-only.

Bring the desktop side up after plugging in:

```sh
tools/host-usbnet.sh --wait        # waits up to 60 s for the interface, then configures it
ping 172.16.42.1
```

If nothing appears: check `lsusb | grep 1d6b:0104` and `dmesg | tail`. On the project's tablet the
Type-C port sometimes came up as *source/host*; the link only worked after unplugging and
replugging the cable (the rootfs sets `preferred_role=sink`, see `rootfs/onboard/hw-up.sh`). If the
gadget enumerates but has no carrier, the tablet switches NCM → ECM by itself after about 16 s.

## Your SSH key (never reuse someone else's)

The project's own keys are not in this repository. Make your own before building the rootfs:

```sh
ssh-keygen -t ed25519 -f ~/.ssh/omarchy-tablet_ed25519 -C omarchy-tablet
```

- `rootfs/build-rootfs.sh` puts `~/.ssh/omarchy-tablet_ed25519.pub` (or `$TABLET_SSH_KEY.pub`) into
  `/root/.ssh/authorized_keys` and `/home/alarm/.ssh/authorized_keys` of the image, and generates
  **new** SSH host keys for the image. It writes a ready known_hosts line to
  `work/rootfs/hostkeys/known_hosts`; install it once:

  ```sh
  cp work/rootfs/hostkeys/known_hosts ~/.ssh/omarchy-tablet_known_hosts
  ```

  `work/rootfs/hostkeys/` holds the host *private* keys too: keep it private, never commit it.
- To add or replace a key on a tablet that is already running, append the `.pub` line to
  `/root/.ssh/authorized_keys` on the tablet (over an existing ssh session, or from the initramfs
  rescue shell where the root filesystem is mounted at `/newroot`).
- `sshd` on the tablet accepts keys only (`rootfs/overlay/etc/ssh/sshd_config.d/10-omarchy-tablet.conf`).

`tools/tablet` reads:

| Variable | Default |
|---|---|
| `TABLET_SSH_KEY` | `~/.ssh/omarchy-tablet_ed25519` |
| `TABLET_KNOWN_HOSTS` | `~/.ssh/omarchy-tablet_known_hosts` |
| `TABLET_ADDRESS` | `172.16.42.1` (set it to the Wi-Fi address once Wi-Fi works; `HostKeyAlias=172.16.42.1` keeps the same host key entry) |

Examples:

```sh
tools/tablet uname -r                 # 5.10.149-android12-9-00031-g8d44cb1a9e42-ab9703858
tools/tsh 'cat /run/initramfs.log'    # while the tablet sits in the initramfs rescue shell
tools/tsh-put stage1.cpio /tmp/stage1.cpio 0644
```

## Internet for the tablet over the cable

`python3 tools/usb-proxy.py` on the desktop serves a proxy on 172.16.42.2:3128. Pointing the
tablet's `http_proxy`/`https_proxy` at `http://172.16.42.2:3128` is the intended use; the notes do
not record the exact tablet-side commands. A desktop firewall that blocks inbound connections from
the tablet will block this too; the project worked around a blocking firewall with an SSH reverse
tunnel (`ssh -R`) instead (exact command not recorded).
