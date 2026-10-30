<!-- Copied from the private working notes, 2026-10-28; "parent"/"root" = the orchestrating session that operated the tablet. -->
# Temporary SDP workaround: installed

The parent installed `usb-host-current.py` and
`../systemd/omarchy-usb-host-current.service` after reviewing the offline tests.
The helper and unit are root-owned; the unit is enabled. Live adoption and
clean stop/start passed (original policy -1 restored, then 500000 reapplied),
and the controlled 22:01 reboot automatically applied 500000 to the real chip.
The preparing subagent performed no device operations.
This is a temporary workaround for the board's CSCI USB-current override; the
exact driver proof is summarised in `docs/11-power-battery.md` (the extracted vendor
modules are not redistributed).

The helper checks the exact MT8781V/CA model, MT6789 compatible, SouthChip supply
and both loaded149 module build IDs before any hardware-policy write. Every2s
it reads only the known standard supply attributes and the platform policy
getter. It writes only standard master-charger `input_current_limit`.

- Online SDP with policy-1: record ownership, then request500000 microamps.
- Any unowned policy other than-1, including0 or500000: leave it untouched.
- Owned500000 during unknown/offline/transient VBUS: hold it without rewrites.
- Owned500000 with known CDP/DCP/PD: restore original-1 and remove ownership.
- Any observed change away from owned500000: remove ownership without writing.
- SIGTERM/normal stop: restore owned policy only. ExecStopPost retries cleanup
  after failures/SIGKILL. Module/sysfs uncertainty prevents policy writes.

Only ownership changes are logged. The unit has After=omarchy-hw.service,
no gadget ordering, Restart=on-failure, RuntimeDirectory mode0700 and
RuntimeDirectoryPreserve=restart. Ownership is an atomic0600 JSON file:

```json
{"version":1,"boot_id":"CURRENT-BOOT-UUID","original_policy":-1,"cap":500000}
```

Path: `/run/omarchy-usb-current/owned.json`. A new process validates file
ownership/mode/schema and current boot ID. A record from another boot is
discarded without restoring or adopting an existing500000. Advisory flock
serializes this helper's loop, --once, --restore and explicit adoption.

## Root deployment and adoption procedure

Copy the reviewed helper to
`/opt/omarchy-tablet/diagnostics/usb-host-current.py` with root ownership0755,
and the reviewed unit to `/etc/systemd/system/omarchy-usb-host-current.service`
with root ownership0644. Back up existing destination files first. Run target
`systemd-analyze verify`, then `systemctl daemon-reload`.

For an already verified root trial whose original policy was-1 and current
policy500000, **cancel the trial's existing rollback timer first**. Otherwise
that older owner could restore the policy after adoption. Before starting the
service, root can create its ownership record without briefly removing the cap:

```sh
install -d -o root -g root -m0700 /run/omarchy-usb-current
/opt/omarchy-tablet/diagnostics/usb-host-current.py --adopt-existing --original-policy=-1
cat /run/omarchy-usb-current/owned.json
systemctl start omarchy-usb-host-current.service
```

Adoption requires verified hardware, online SDP and existing policy500000. It
writes only the ownership file, with the actual current boot ID; it never writes
charger sysfs. Its original-policy assertion is explicit and must come from
root's retained readback. The unit never carries an adoption flag. Normal
startup without adoption applies a cap only when the policy is-1.

Verify the platform policy is500000 and standard hardware limit at most500000,
then observe physical VBUS stability with both gadget and playback use. The SC
PD override can bypass the normal ceiling, so service ownership/policy readback
alone does not establish actual current. Enable the unit at boot only after
parent-owned runtime validation and clean stop/start tests.

To disable: stop the unit, verify policy-1 if it still owned500000, then disable
the unit. `--restore` also performs one owned-only cleanup. It must not restore
an unowned500000 or another writer's positive/zero limit. Do not restore-1 via
the platform `input_current` store, which converts negatives to zero.

## Limits and offline validation

Sysfs provides no compare-and-swap transaction against independent thermal
writers. The helper checks the policy immediately before each write and stops
claiming any observed foreign value, but cannot distinguish an independent
writer setting the identical500000 value while ownership exists. It does not
replace a thermal controller. Missing or changed identities fail visibly and
prevent writes; a cap may remain applied until identity/control recovery allows
verified cleanup.

Offline tests use a temporary fake sysfs tree and simulated policy/hardware
readbacks. They cover SDP application, idempotence, foreign caps, unknown/offline
holding, CDP/DCP/PD restoration, external changes, restart/stop, boot-ID changes,
explicit adoption, invalid identity/USB state, private state permissions and unit
ordering/cleanup. They do not claim target validation:

```sh
python3 power/tests/test-usb-host-current.py
python3 -m py_compile power/tools/usb-host-current.py
```
