<!-- Design ledger copied from the private working tree. Paths are relative to that tree's root;
     this directory is design/touch-review/ there. -->
# Design-only work ledger — 2026-10-28

All writes were confined to `design/touch-review/` (in the private working tree; published here as `shell-plugin/design/`). No SSH, tablet access, OS configuration, service changes, global installs, server, publication, or subagents.

Read PROJECT.md and the UI sources / docs listed in README. Inspected the incumbent screenshot `ui/evidence/tablet-touch-abc-live.png` with view_image, without editing it. Read Product Design index/get-context, Omarchy and Impeccable guidance. User’s explicit standalone-HTML brief takes precedence over image-first / selection / server workflows; parent had resolved the brief. This is an Operate-mode review artifact with three pinned navigation strategies.

Budget: under 1 MiB of durable design outputs; an isolated temporary jsdom dependency tree for checks only. Starting free space reported 5.3 GiB; no firmware/build paths were scanned recursively.

Commands (project root unless specified):

```sh
pwd
rg --files -g PROJECT.md -g AGENTS.md "$PROJECT"
cat PROJECT.md
rg --files ui omarchy-port -g '!**/upstream/**' -g '*.py' -g '*.md' -g '*.png' -g '!**/build/**'
cat ui/TOUCH-UI.md ui/TOUCH-WORK.md
sed -n '1,170p' ui/rootfs/tablet-ui
cat ui/rootfs/TabletButton.qml
sed -n '1,110p' ui/wvkbd/tablet-layouts.h
rg --files omarchy-port/upstream/shell
rg --files ui
rg -n 'keyboard|touchbar|tabletnav|layer|focus|modifier' ui/rootfs/install-touch-ui.py
df -h "$HOME"
mkdir -p design/touch-review
```

Used structured web search/open for official HIG and toolkit references. Apple’s JS pages linked Markdown endpoints; the web tool rejected the text/markdown content type, so Python urllib read the same official `.md` endpoints for gestures, virtual-keyboards, multitasking, accessibility, sidebars and tab-bars. No private data was read. Official Hyprland wiki legacy routes returned 404; these were not used as supporting references. Current official Quickshell, Qt, Omarchy, Hyprlock, wvkbd, portal and NetworkManager references are linked in README / HTML.

Wrote inline HTML/CSS/JS via a quoted heredoc; subsequent local Python replacements fixed keyboard caret preservation, literal terminal Ctrl+C behavior, target sizes, modal return focus / inert scope, and shell-only Back behavior.

```sh
"$IMPECCABLE"  # impeccable 4.3.1 skill script (design linter) context --target design/touch-review/omarchy-touch-review.html
"$IMPECCABLE"  # impeccable 4.3.1 skill script (design linter) detect --json design/touch-review/omarchy-touch-review.html
python3 design/touch-review/embed-icons.py
npm install --prefix design/touch-review/.validation-deps --no-save --ignore-scripts --no-audit --no-fund jsdom@26.1.0
node design/touch-review/validate.cjs
```

Icon authoring retrieved an embedded subset from Lucide’s official repository. Missing upstream names were resolved to existing official aliases (audio-lines / text-cursor / ellipsis) before delivery. The full license is inline. The helper now exits without network if icons are already embedded.

The first DOM check exposed a caret reset when switching keyboard pages; fixed by preserving the selection before focusin reconstruction. All 25 checks then passed. A later Back / dialog accessibility adjustment justified a bounded repeat. No screenshot, native focus or performance claim is based on jsdom.

Final checks: `node design/touch-review/validate.cjs`, local contrast-token calculations recorded in `contrast-tokens.json`, syntax / standalone / inventory checks within that DOM pass, and SHA-256 / file-size inventory below. Temporary dependencies were removed with a scoped Python shutil.rmtree of `.validation-deps` after the successful run. No browser or preview tools were used.

## Option A refinement, 2026-10-29

User selected A as a starting point and requested refinement. Original HTML was
archived locally. Refined A only (plus narrow-width review-selector wrapping),
preserving all three directions, all 18 surfaces, and the existing mock input
engine. Four additional meaningful checks cover dock routing/current state,
compact keyboard navigation, removal of in-OS design banners, and recents
resume actions. Total: 29 checks, 108 DOM combinations. No device operations.
Home presentation spot-checked at 728/375px; console output empty. This is not
a full visual or accessibility audit. Impeccable detector ran once and retained
two outer no-inset warnings required by the inline host presentation contract;
see refinement-detector.json. No detector result is used as quality proof.

## Option A second refinement, 2026-10-29

User asked for a second review of A, then an A-only refinement. Archived the
prior A as omarchy-touch-review-a-pass1.html. Rendered all 18 A surfaces in
both orientations with system Chromium via a temporary playwright-core install
in /tmp (removed afterwards), reviewed screenshots, then applied asserted string
replacements limited to `d==='a'` branches and `.a`-scoped CSS. Two existing
checks encoded old A behaviour (explicit Keys on Search; dock visible in apps)
and were updated; four checks were added (apps fill canvas / dock steps aside,
tap-to-type with one Hide control, one-tap search Back, pair-only window
actions). 33 checks, 108 DOM combinations. Re-rendered every A surface plus
app, keyboard, search and split flows: no console errors. No device operations.

## Option A gesture text editing, 2026-10-29

User asked that selection / context / clipboard be triggered by long press or
double tap. A-only: replaced the visible edit button row with a floating edit
menu opened by double-tap or 500 ms long-press on words or text fields;
right-click and Menu key / Shift+F10 are non-touch routes. Secure fields omit
Copy/Cut. One check added (34 total). Exercised real pointer gestures in system
Chromium via a temporary playwright-core in /tmp; fixed a stale click-suppression
bug found there. No device operations.
