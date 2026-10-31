# 07 - Audio (built-in speaker and microphone via PipeWire)

The built-in speaker and the built-in microphone work through PipeWire/WirePlumber on the stock vendor
ALSA stack (MT6789 AFE + MT6366 codec, ALSA card `mt6789-mt6366`, kernel
driven by the shipped vendor modules). No kernel, module, firmware or
partition change is involved: the fix is entirely userspace configuration.

Work dates: 2026-10-28 (speaker), 2026-10-30 (microphone). Files: [`audio/`](../audio).

## Status

| Function | Status | Evidence / notes |
| --- | --- | --- |
| Built-in speaker, PipeWire default sink "Tablet speakers" | **Works** | After a normal reboot the user played a YouTube video in Chromium and confirmed: "just played a yt video and i hear the video audio!" |
| Profile/sink survives reboot and WirePlumber restart | Works | `wpctl status` shows `Built-in Audio Tablet speakers`, active profile `HiFi`, volume retained |
| Volume control | Works | Software volume in PipeWire; the packaged Omarchy shell `omarchy.audio` bar widget is placed on the touch bar (see [docs/04](04-omarchy.md) and [docs/10](10-touch-ui.md)) |
| Stereo channel separation on the speaker(s) | Unverified | Vendor policy exposes the speaker as stereo; physical L/R separation was never checked |
| Built-in microphone, PipeWire source "Internal microphone" | **Works** (measured, not yet tried by voice) | Recorded through PipeWire (`pw-record`) while the speaker played a 440 Hz tone: the tone is 140x above the quiet level, at the right pitch and length. Level is modest (room noise about -73 dBFS, the speaker tone about -58 dBFS at maximum analog gain); raise the source volume in PipeWire if voices are quiet |
| Second analog mic input (AIN2, vendor "mic2") | No signal | Silent at the electrical noise floor (-87 dBFS); nothing appears to be wired to it on this unit |
| 3.5 mm headphones, headset mic, jack detection | Unvalidated | Never tested; jack input device exists but its switch bits were not confirmed |
| Bluetooth audio | Unvalidated | Not attempted |
| Raw ALSA `hw:0,0` with `snd_pcm_drain()` | **Unsafe** | Resets the tablet (watchdog reboot). Use the `tablet_speaker` PCM or PipeWire |
| S32LE playback and capture | Broken | Playback runs at roughly half speed; capture comes out at double pitch (two real frames per S32 frame). S16LE is forced on both the sink and the source |

## Prerequisites

- The native Arch Linux ARM + Omarchy system from the earlier docs is
  installed and boots to the Hyprland desktop as user `hypr` (UID 1001; the
  scripts here assume that user and UID).
- Network/USB SSH access to the tablet. The shared helper is `tools/tablet`
  (root SSH to `172.16.42.1`, key from
  `${TABLET_SSH_KEY:-$HOME/.ssh/omarchy-tablet_ed25519}`).
- Tablet packages from the Arch Linux ARM repositories: `pipewire-audio`,
  `pipewire-alsa`, `pipewire-pulse`, `rtkit`, plus `wireplumber`,
  `alsa-utils` (`alsactl`, `alsaucm`, `amixer`) and `alsa-ucm-conf`.
  Versions in use when this was validated: PipeWire 1.6.10, WirePlumber
  0.5.18, alsa-lib 1.2.16.1.
- Nothing has to be copied from the vendor partitions to make the speaker
  work: the UCM in this repo contains only mixer control names and values.
  If you want to re-derive or extend the route (see "Open work"), extract the
  vendor files from your own device as described under
  [Obtaining vendor reference data](#obtaining-vendor-reference-data-from-your-own-device).

## Files in `audio/`

| File | Installed as | Purpose |
| --- | --- | --- |
| `ucm2/MediaTek/mt6789-mt6366/mt6789-mt6366.conf` | `/usr/share/alsa/ucm2/MediaTek/mt6789-mt6366/` | UCM card file: private `tablet_speaker` PCM with `drain_silence 0`, single `HiFi` use case, safe boot sequence (amp off, muxes open) |
| `ucm2/MediaTek/mt6789-mt6366/HiFi.conf` | same directory | `HiFi` verb (DL1 to ADDA, plus the mic route AIN0 to UL1), device `Speaker` (headphone-buffer speaker route, fixed gain 10) and device `Mic` (capture PCM `hw:N,9`, 2 ch, 48 kHz) |
| `tablet-speaker.conf` | `/etc/alsa/conf.d/99-tablet-speaker.conf` | Standalone `tablet_speaker` PCM for direct ALSA diagnostics |
| `51-tablet-speaker.conf` | `~hypr/.config/wireplumber/wireplumber.conf.d/` | WirePlumber rule: UCM on, pro-audio off, probe rate 48000, speaker sink and mic source forced to S16LE. Its first comment line ("Discovery candidate ...") is stale; the file is byte-identical to the installed copy |
| `install-ucm.sh` | run on the tablet | Copies the UCM tree, creates the `conf.d/<driver>/<name>.conf` lookup links from `alsactl info`, installs the two `.conf` files above |
| `test-speaker.sh` | run on the tablet | Plays a WAV through the normal PipeWire output with `pw-play` (no mixer changes) |
| `make-tone.py` | run anywhere | Generates the quiet test tone (48 kHz, S16LE, stereo, 440 Hz, -30 dBFS; 1.5 s left, 1.5 s right, 1.5 s both). Reconstructed from the recorded parameters; the original generator was not kept, so the output is not byte-identical to the tone used during bring-up |
| `SHA256SUMS` | - | Hashes of the four config files; they match the files verified on the tablet after the confirming reboot |
| `diag/audio-status.sh` | run on the tablet as root | Read-only snapshot: packages, groups, cards, PCM state, PipeWire, route readbacks, recent logs |
| `diag/speaker-route-status.sh` | run as root while audio plays | Read-only: PCM status, mixer readbacks, GPIO49/pinctrl, DAPM widgets, amp events, live DT amp properties |
| `diag/probe-jack.sh` | run on the tablet | Read-only: reports which switch events the "mt6789-mt6366 Headset Jack" input device advertises |
| `diag/dump-dt.sh` | run on the tablet | Read-only: packs the live device tree to `/tmp/live-dt.tgz` |
| `diag/pcm-boundary.c` | build on the tablet | The marked zero-data ALSA diagnostic that located the drain reset. **Its `drain` mode reboots the tablet by design of the test**; see Pitfalls |

## Reproduction

All tablet commands run as root unless noted. "Copy to the tablet" means any
method you like; for example from the repo root:

```bash
tar -C audio -cf - . | tools/tablet 'mkdir -p /opt/omarchy-tablet/audio && tar -C /opt/omarchy-tablet/audio -xf -'
```

(This transfer command is a suggestion; the original files were staged under
`/opt/omarchy-tablet/` by the project's own tooling.)

### 1. Install the PipeWire audio packages and grant ALSA access

Recorded commands (run as the desktop user for `omarchy pkg add`):

```bash
omarchy pkg add pipewire-audio pipewire-alsa pipewire-pulse rtkit
sudo usermod -aG audio hypr
sudo reboot
```

Why: the Omarchy package set on this port lacked PipeWire's ALSA backend, so
Chromium played video with no sink at all (`wpctl status` listed no audio
devices). Separately, `/dev/snd/*` are `root:audio 0660` and this vendor
kernel's device filesystem returns `Operation not supported` for POSIX ACLs,
so systemd-logind's normal `uaccess` grant cannot give the session access.
Membership in `audio` is required.

The user manager and its PipeWire processes keep their old supplementary
groups until a fresh session, hence the reboot. (During bring-up the nodes'
group was temporarily changed to `hypr` to avoid a reboot; that is not needed
and not persistent.) The project's user-setup script performs the `usermod`;
see [docs/04](04-omarchy.md).

Verify after reboot:

```bash
id hypr                              # contains audio
ls -l /dev/snd/controlC0             # crw-rw---- root audio
grep Groups /proc/$(pgrep -u hypr -x wireplumber)/status   # includes the audio GID
```

Expected at this point: `Built-in Audio` appears in `wpctl status`, but with
only `off` and `pro-audio` profiles and a Dummy Output. Do **not** select
`pro-audio` (see Pitfalls).

### 2. Install the speaker-only UCM profile, scoped PCM and WirePlumber rule

On the tablet, from the copied `audio/` directory:

```bash
cd /opt/omarchy-tablet/audio
sh install-ucm.sh            # optional arg: UCM root, default /usr/share/alsa/ucm2
```

What it does:

- copies `ucm2/MediaTek/mt6789-mt6366/` to `/usr/share/alsa/ucm2/MediaTek/`;
- reads `alsactl info 0` and links
  `/usr/share/alsa/ucm2/conf.d/<driver_name>/<name>.conf` to the card file.
  On this tablet driver_name, name and longname are all `mt6789-mt6366`
  (card id `mt6789mt6366`), so the result is
  `/usr/share/alsa/ucm2/conf.d/mt6789-mt6366/mt6789-mt6366.conf`;
- installs `tablet-speaker.conf` as `/etc/alsa/conf.d/99-tablet-speaker.conf`;
- installs `51-tablet-speaker.conf` into
  `/home/hypr/.config/wireplumber/wireplumber.conf.d/` owned by `hypr`;
- prints `alsaucm -c hw:0 list _verbs` and the HiFi devices (this sets the
  HiFi verb, i.e. the DL1-to-ADDA mixer switches only; it plays nothing).

Expected check output: one verb `HiFi`, one device `Speaker` ("Tablet
speakers").

Then restart the user audio services as `hypr` (or simply reboot):

```bash
systemctl --user restart wireplumber pipewire pipewire-pulse
```

The installed UCM files are not owned by any package (`pacman -Qo` reports
"No package owns"), so a future `alsa-ucm-conf` upgrade will not overwrite
them, and will not remove them either.

### 3. Verify

As `hypr`:

```bash
wpctl status          # Sinks: "* Built-in Audio Tablet speakers"
pactl list cards | grep -E 'HiFi: Tablet speakers|Active Profile'
                      # HiFi: Tablet speakers and microphone (sinks: 1, sources: 1, ...)
                      # Active Profile: HiFi
pactl get-default-sink   # alsa_output.platform-soc_sound.HiFi__Speaker__sink
```

Play the test tone through PipeWire (no mixer changes):

```bash
python3 -I make-tone.py /opt/omarchy-tablet/audio/tone-440-m30dBFS-L-R-LR.wav
sh /opt/omarchy-tablet/audio/test-speaker.sh     # root re-execs as hypr
```

`test-speaker.sh` refuses `HEADPHONE=1` and `PCM=...` overrides on purpose.
Then play anything in Chromium; that is the check the user confirmed.

While something is playing, as root:

```bash
cat /proc/asound/card0/pcm0p/sub0/hw_params
#   format: S16_LE / channels: 2 / rate: 48000 (48000/1)
sh diag/speaker-route-status.sh
#   HPL Mux / HPR Mux = LoudSPK Playback, LOL Mux = Open,
#   Headset Volume = 10,10, Ext_Speaker_Amp Switch = on,
#   AFE pinctrl state extamp_pullhigh (GPIO49)
```

Recorded good state during validated playback: PCM RUNNING,
MMAP_INTERLEAVED S16_LE, stereo 48000 Hz, period 1024, buffer 32768 frames;
DAPM widgets DACL/DACR, AIF1 Playback, Headphone L/R Ext Spk Amp and
Ext_Speaker_Amp On; LINEOUT L Off.

`diag/audio-status.sh` (root) gives a read-only summary of packages,
permissions, card, PipeWire and mixer state. Compare installed files with
`sha256sum -c` against `audio/SHA256SUMS` (adjust paths).

Microphone check (as `hypr`; play the tone in a second shell while recording):

```bash
wpctl status          # Sources: "* Built-in Audio Internal microphone"
pw-record --rate 48000 --channels 2 --format s16 /tmp/mic.wav &
sleep 1; pw-play /opt/omarchy-tablet/audio/tone-440-m30dBFS-L-R-LR.wav; sleep 1; kill %1
# while recording, as root: /proc/asound/card0/pcm9c/sub0/hw_params shows S16_LE, 2 ch, 48000
```

Listen to `/tmp/mic.wav` or measure it: the 440 Hz tone must come out at
440 Hz. If it is an octave high, the S16LE rule for the source is missing.

## The fixes, in order, and why each was needed

1. **Missing PipeWire packages and audio-group access** (step 1). Without
   the ALSA SPA plugin there is no ALSA device in PipeWire at all; without
   `audio` membership (no ACL support) the daemons cannot open `/dev/snd`.

2. **Independent speaker-only UCM profile.** The first UCM written for this
   card (HiFi with Speaker, Headphones, Mic and Headset devices) passed
   `alsaucm` lookups but PipeWire still offered only `off`/`pro-audio`.
   PipeWire's ACP builds one profile per *maximal* compatible device subset;
   with Speaker/Headphones and Mic/Headset conflicting, every generated
   profile contained one capture device. ACP then opens and configures every
   mapping, and the capture PCM `hw:0,9` (`Capture_1`) failed `snd_pcm_hw_params`
   with `EINVAL`, so all four profiles were rejected even though the output
   PCM `hw:0,0` configured fine. Proven with:

   ```bash
   spa-acp-tool -vvv -c 0 -p 'api.acp.auto-profile=false api.acp.auto-port=false api.acp.disable-pro-audio=true api.alsa.split-enable=true device.profile=off' list-profiles
   ```

   which logs, per profile, `snd_pcm_hw_params failed: Invalid argument`,
   `mapping 'HiFi: Headset: source': input PCM open failed`,
   `Profile HiFi (...) not supported`. (This probe opens and configures PCMs
   without playing data.) The fix is a profile with no capture device at all.
   Capture must be reintroduced only after `hw:0,9` works on its own.

3. **Scoped PCM disabling drain-time silence filling.** Direct playback on
   `hw:0,0` reset the tablet (watchdog reboot) at the end of the stream. The
   marked diagnostic `diag/pcm-boundary.c` showed that with stop_threshold at
   boundary, `snd_pcm_drop`, `hw_free` and `close` succeed, while `drain`
   mode hangs inside the SW_PARAMS ioctl that alsa-lib issues *before* the
   DRAIN ioctl:

   ```text
   PCM_MARK 413.246543375 ENTER snd_pcm_drain(p)
   ioctl(4</dev/snd/pcmC0D0p<char 116:2>>, SNDRV_PCM_IOCTL_SW_PARAMS, 0x7ffd86b6d0   <- tablet gone
   ```

   alsa-lib 1.2.16.1 `snd_pcm_hw_drain` adds about 100 ms of end silence by
   setting `silence_threshold`/`silence_size` via SW_PARAMS. The kernel
   (`snd_pcm_sw_params` -> `snd_pcm_playback_silence`) then fills silence
   immediately into the DMA buffer. ASoC on this 5.10 kernel installs no
   `fill_silence` callback, so the generic path uses ordinary `memset`. For
   this stream the vendor driver uses on-chip SRAM, which the shipped
   `snd-soc-mtk-common.ko` maps with `of_iomap` (Device memory). arm64
   `memset` can use `DC ZVA`, which faults on Device memory; I/O memory needs
   `memset_io`. This chain is source- and disassembly-supported; the exact
   faulting instruction was never captured, so treat it as the best
   explanation, not a recovered backtrace.

   The fix is a named hardware PCM with the alsa-lib `hw` plugin option
   `drain_silence 0`, which skips that pre-drain silence request while
   keeping real drain. With it, the same diagnostic completed DRAIN,
   HW_FREE and CLOSE with no reset. It must be defined **inside the UCM
   `LibraryConfig`** (`tablet_speaker` in `mt6789-mt6366.conf`, with
   `SubstiConfig` expanding `${CardId}`), because UCM's private library
   configuration does not inherit arbitrary PCM aliases from the system ALSA
   config. The `/etc/alsa/conf.d/99-tablet-speaker.conf` copy exists only so
   direct ALSA diagnostics (`aplay -D tablet_speaker`) are safe too.
   Reference: the `drain_silence` option in the
   [ALSA PCM plugin documentation](https://alsa-project.org/alsa-doc/alsa-lib/pcm_plugins.html).

4. **Vendor-selected headphone-buffer speaker route.** The first route used
   the generic vendor `speaker_output` path (line-out: `LOL Mux = Playback`
   plus the external amp). Streams ran, the amp widget was On and GPIO49 was
   driven, but the speaker was silent. The board's populated options file
   `AudioParamOptions_vext.xml` selects `MTK_AUDIO_SPEAKER_PATH = int_hp_buf`,
   `SPK_PATH_HP = yes` and one speaker, i.e. the speaker amp is fed from the
   codec's headphone buffers. The matching vendor sequence
   (`headphoneSpeaker_output` in `audio_device.xml`) is
   `DAC In Mux = Normal Path`, `HPL Mux = LoudSPK Playback`,
   `HPR Mux = LoudSPK Playback`; the vendor volume table maps the speaker
   gain to `Headset Volume`. `HiFi.conf` implements exactly this, keeps
   `LOL Mux` Open, sets `Headset Volume` to a fixed 10,10 and switches
   `Ext_Speaker_Amp Switch`, whose DAPM event in the shipped machine driver
   drives the amp enable (GPIO49, `extamp_mode = 2`: a two-pulse sequence
   ending high). Volume is software volume in PipeWire.

5. **S16LE playback.** With PipeWire's default S32LE the parameters were
   accepted but the hardware pointer advanced at about half the requested
   rate: a 6-second file took roughly twice as long (amp on at uptime 200.96,
   off at 213.87). With `audio.format = "S16LE"` on the speaker node the file
   returns in the correct time and the pointer rate matches 48 kHz. The AFE
   probe sets `memif_32bit_supported = 0`, so S32LE selects a vendor
   24-in-32 mode; the exact defect in that path is not identified.

The speaker became audible only once 4 and 5 were both in place after a
normal reboot; earlier "successful" tone runs were silent and are not
counted as speaker successes.

### Microphone, 2026-10-30

1. **EINVAL was a missing route, not bad parameters.** `hw:0,9` accepts
   S16LE/48 kHz/2 ch as soon as the uplink route is connected (`ADDA_UL_Mux`
   MTKAIF, `PGA L/R Mux` AIN0, `ADC L/R Mux` Left/Right Preamplifier,
   `UL1_CH1 ADDA_UL_CH1` and `UL1_CH2 ADDA_UL_CH2` on), the same pattern as
   the playback EINVAL with the routes off. ACP probes the PCM before any UCM
   device is enabled, so the route sits in the `HiFi` verb's EnableSequence.
   Routes only connect DAPM paths; nothing powers up until a PCM opens
   (DAPM showed `MIC_BIAS`, `AIN0`, `PGA L`, `ADC L` on only while recording).
2. **Which input is the mic.** With a 440 Hz tone on the speaker, AIN0
   (vendor `builtin_Mic_Mic1`) picked it up 100-150x above its quiet level;
   AIN2 (`mic2`) stayed at the -87 dBFS electrical floor. `Mic Type Mux` ACC
   and DCC measured the same. `PGA1/PGA2 Volume` 4 (the maximum, 24 dB) is
   used because the level is still low.
3. **S32 doubles the pitch.** PipeWire first opened the PCM as S32_LE; the
   tone came out at 880 Hz and the file held half the frames. The capture
   DMA delivers 16-bit samples regardless, so `51-tablet-speaker.conf` forces
   `audio.format = "S16LE"` on `alsa_input.platform-soc_sound.HiFi__Mic__source`,
   as it already did for the speaker sink.

Nothing in this step writes PMIC registers, VOW/speech controls or
`PMIC_REG_CLEAR`. Direct `arecord` captures did not trigger the watchdog
reset that raw playback drain does.

## Pitfalls and lessons

- **Never run raw `aplay -D hw:0,0` (or any ALSA client on `hw:0,0` that
  drains).** It reboots the tablet. `aplay` drains at end of file. Use
  `aplay -D tablet_speaker ...` or PipeWire. PipeWire's own playback path
  stops with `snd_pcm_drop` and goes through the UCM PCM anyway. Other
  programs that open the raw `hw` device directly remain at risk until the
  driver or kernel is fixed.
- **`alsaucm list _verbs` succeeding does not mean PipeWire will create a
  profile.** ACP probes every PCM in every generated profile; use
  `spa-acp-tool ... list-profiles` (above) to see the real reason.
- **Do not select `pro-audio`.** The generic profile exposes unrelated
  vendor PCMs (hostless, speech, SCP/DSP, VOW). `51-tablet-speaker.conf`
  disables it for this card.
- **Transport success is not audibility.** Several tone runs exited 0 with
  the amp on and were silent because the analog route was wrong. Have a
  person listen.
- **The vendor gain index runs opposite to the kernel TLV.** The vendor
  table says 0 = +8 dB ... 8 = 0 dB ... 18 = -10 dB (31 = mute); the kernel
  TLV claims the reverse. 9 and 10 are moderate under both readings; never
  use 0..7. There is no speaker protection DSP (no smart amplifier is
  fitted), so keep the analog gain conservative and use software volume.
- **Do not drive GPIO49 or write PMIC registers by hand.** The machine
  driver pulses the enable pin itself; a plain GPIO-high from userspace is
  not equivalent, and the AFE owns the pin.
- **UCM `LibraryConfig` does not see `/etc/alsa/conf.d` PCM aliases.** The
  safe PCM must be defined in the UCM card file.
- **PCM HW_PARAMS can return EINVAL if the DL1-to-ADDA route is off.** One
  diagnostic run failed that way after ACP had left routes off; enabling the
  HiFi verb's route resolved it.
- **Pstore after a reset may be stale.** After the first audio reset the
  pstore console was byte-identical (same SHA256) to one from an earlier,
  unrelated session. Compare hashes and internal uptime before attributing a
  backtrace.
- **Keep touch/input readers (`evtest`) away from audio tests.** One early
  reset happened while `evtest` was reading the touch device during
  playback; it was not explained, so the two were kept separate afterwards.
- Membership changes need a fresh session; restarting PipeWire inside the
  old user manager keeps the old groups.

## Rollback

Recovery from a playback-induced reset is automatic: the watchdog reboots
the tablet and the desktop comes back. To remove the audio configuration:

```bash
# as root on the tablet
rm -r /usr/share/alsa/ucm2/MediaTek/mt6789-mt6366
rm -r /usr/share/alsa/ucm2/conf.d/mt6789-mt6366     # or conf.d/<driver_name>/ from alsactl info
rm /etc/alsa/conf.d/99-tablet-speaker.conf
rm /home/hypr/.config/wireplumber/wireplumber.conf.d/51-tablet-speaker.conf
# as hypr
systemctl --user restart wireplumber pipewire pipewire-pulse
```

Check first that nothing else lives in those UCM directories
(`pacman -Qo` on them should report no owner). The packages and the `audio`
group membership can stay; they are harmless without the profile. On the
original device a backup of the earlier UCM was kept under
`/opt/omarchy-tablet/backups/`; a fresh reproduction has no earlier profile
to restore.

## How it works

```text
app -> PipeWire (S16LE, 48 kHz, software volume)
    -> UCM HiFi/Speaker -> tablet_speaker (hw:mt6789mt6366,0, drain_silence 0)
    -> AFE DL1 (memif in SRAM) -> ADDA_DL -> MT6366 DAC
    -> HPL/HPR "LoudSPK Playback" -> external analog amp (GPIO49, DAPM event)
    -> speaker
```

- The board has no smart amplifier: no I2C amp device is present, and the
  DT `mediatek,speaker-codec` node is empty; the aw87xxx helpers find no
  instance and their return is ignored by the amp event, which drives the
  GPIO first. No speaker firmware, calibration or SCP DSP is needed.
- The UCM boot sequence leaves the amp off and every output mux Open, so the
  card is quiet until PipeWire enables the Speaker device.
- WirePlumber matches the card by `device.name = alsa_card.platform-soc_sound`
  and the sink by its exact node name.

## Obtaining vendor reference data from your own device

Nothing below is needed to use the profile. It is how the route was
derived and how to continue the open work. Do not redistribute the results;
they are vendor files.

- Mixer controls (names, ranges, current values):
  `amixer -c 0 contents > amixer-contents.txt`.
- Card identity for UCM lookup: `alsactl info 0`, `cat /proc/asound/cards /proc/asound/pcm`.
- Vendor audio configuration (`audio_device.xml` with the per-path mixer
  sequences, `audio_policy_configuration.xml`, and `audio_param/*.xml`
  including `AudioParamOptions_vext.xml` and `Volume_AudioParam.xml`): these
  live on the vendor partition, normally under `/vendor/etc/` and
  `/vendor/etc/audio_param/` on Android. The exact path on your mounted
  vendor image may differ; locate them with
  `find / -xdev -name 'AudioParamOptions_*.xml' 2>/dev/null` against the
  mounted vendor partition or an extracted image (see [docs/01](01-device-and-backups.md) and
  [docs/03](03-boot-and-rootfs.md)). Key lines: `MTK_AUDIO_SPEAKER_PATH`, `SPK_PATH_HP`,
  `MTK_AUDIO_NUMBER_OF_SPEAKER`, and the `headphoneSpeaker_output` path.
- Shipped audio modules for disassembly (`mt6789-mt6366.ko`,
  `snd-soc-mt6789-afe.ko`, `snd-soc-mtk-common.ko`, `snd-soc-mt6366.ko`,
  `aw87xxx_sound.ko`, `mtk-sp-spk-amp.ko`): `modinfo -n <module>` shows the
  path of each loaded module (on Android they come from
  `/vendor_dlkm/lib/modules/`). Copy read-only. The analysis used
  `nm` and `llvm-objdump -dr --no-show-raw-insn --disassemble-symbols=<sym>`
  (e.g. `mt6789_mt6366_spk_amp_event`, `AudDrv_GPIO_EXTAMP_Select`,
  `mtk_audio_sram_init`).
- Live device tree: `sh diag/dump-dt.sh` on the tablet, then on the host
  `mkdir d && tar -C d -xzf live-dt.tgz && dtc -I fs -O dts -o live.dts d`.
  Look at the `sound` node (`extamp_mode`) and the AFE pinctrl states
  `aud_gpio_extamp_on/off`.

Public references used:

- GPL reference kernel for MT6789 (not the Alldocube source; machine amp
  code differs from the shipped module, so conclusions were taken from
  disassembly): Gigaset-dev/android_kernel_gigaset_mt6789 at commit
  `5893f686008d83fbd4809744bd69c34e393c98d6`,
  <https://github.com/Gigaset-dev/android_kernel_gigaset_mt6789>
  (`sound/soc/mediatek/` and `sound/soc/codecs/mt6358.c`).
- alsa-lib 1.2.16.1 `src/pcm/pcm_hw.c` (drain silence):
  <https://github.com/alsa-project/alsa-lib/blob/v1.2.16.1/src/pcm/pcm_hw.c>
- ALSA PCM plugins (`drain_silence`):
  <https://alsa-project.org/alsa-doc/alsa-lib/pcm_plugins.html>
- ALSA UCM interface (`JackDev`, etc.):
  <https://www.alsa-project.org/alsa-doc/alsa-lib/group__ucm.html>
- PipeWire 1.6.10 ACP sources (`alsa-ucm.c`, `alsa-util.c`, `acp.c`,
  `alsa-pcm.c`): <https://gitlab.freedesktop.org/pipewire/pipewire/-/tree/1.6.10/spa/plugins/alsa>
- WirePlumber ALSA configuration:
  <https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/alsa.html>
- Linux I/O memory access rules (no plain memset on I/O mappings):
  <https://www.kernel.org/doc/html/v5.19/driver-api/device-io.html>
- Arm architecture reference (DC ZVA on Device memory faults):
  <https://documentation-service.arm.com/static/644c18cbaa78c007af74fa2c>

## Reproducing the drain diagnosis (optional, causes a reboot)

Only if you want to confirm the drain-silence mechanism yourself. Build
`diag/pcm-boundary.c` on the tablet against alsa-lib (the original build
command was not recorded; something like `cc -O2 -o pcm-boundary
pcm-boundary.c -lasound` is expected to work). It writes 32 x 1024 frames of
digital zeros, S16LE/48 kHz/stereo, period 1024, buffer 4096, with no mixer
or amp changes, and prints `PCM_MARK` entry/return lines to stderr unbuffered.
Modes: `drop` (passes), `drain` on `hw:0,0` (**resets the tablet**),
`safe-drain` on `tablet_speaker` (passes). It was run with the speaker amp
off and the PCM closed, under:

```bash
timeout --signal=TERM --kill-after=2s 5s \
  strace -f -ttt -r -yy -s 64 -e trace=openat,close,ioctl,read,write,poll,ppoll \
  ./pcm-boundary --run safe-drain
```

over SSH with output retained on the host (`ServerAliveInterval=2`,
`ServerAliveCountMax=2`), so the last line before a reset survives. The
host/remote harness used for this hard-coded the private layout and is not
included. A userspace timeout cannot recover a kernel hang; expect a
watchdog reboot for `drain`.

## Open work

- **Microphone tuning.** Works (see "Microphone, 2026-10-30"), but
  only a tone test was measured; check it with a real voice call or
  recording. `Mic Type Mux = ACC` is a guess (ACC and DCC measured the same).
  There is no digital capture gain control in this path; use PipeWire's
  source volume. Headset mic (AIN1, `DCC_ECM_SINGLE`) is untested.
- **Headphones / jack.** Vendor `headphone_output`: `DAC In Mux = Normal Path`,
  `HPL/HPR Mux = Audio Playback`, gain `Headset Volume` (the same control the
  speaker uses, so the fixed speaker gain and the headphone gain interact).
  Check `diag/probe-jack.sh` for `SW_HEADPHONE_INSERT`/`SW_MICROPHONE_INSERT`.
  UCM `JackDev` expects the `/dev/input/by-id` basename, not the
  human-readable name; until that is validated, do not give Headphones a
  higher priority than Speaker or the default sink can be misrouted.
- **Bluetooth audio.** Not attempted.
- **S32LE half-rate** and the **drain-silence SRAM fault** are worked
  around, not fixed. A real fix belongs in the vendor driver/kernel
  (`fill_silence` callback using `memset_io`, or DRAM buffers).
- Physical left/right separation of the speaker output is unverified.
