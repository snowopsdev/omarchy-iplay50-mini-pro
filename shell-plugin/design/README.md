> **Public-repo note:** only the final mock `omarchy-touch-review.html` is included. The archived
> earlier versions linked below (`omarchy-touch-review-initial.html`, `omarchy-touch-review-a-pass1.html`)
> and `refinement-detector.json` were left out. `validate.cjs` needs `jsdom` (see NOTES.md).
> Design choice and how it maps to the native plugin: `docs/10-touch-ui.md`.

# Omarchy tablet touch review

Open [omarchy-touch-review.html](./omarchy-touch-review.html) directly with `file://`. It is one standalone document with inline CSS, JavaScript and licensed SVG icons. No server, build, network connection, remote font, or external asset is needed. This is an authorized **design artifact only**; no tablet or OS changes were made.

## Selected direction: A refinement, 2026-10-29

Home & Dock is the selected starting point. The current HTML refines A across
all 18 surfaces; B and C remain available as references. The original review is
preserved in `omarchy-touch-review-initial.html` (not included).

- Home has one search affordance, more consistent app tiles and a single return-to-work row.
- A six-control dock separates Home/Apps, pinned Terminal/Browser, and Open apps/Keyboard. The active destination is marked; Files remains on Home and in Apps.
- Recent apps have tappable static previews with a separate, explicitly named Close action.
- Search, lists, controls, notifications, dialogs and spacing share quieter solid materials. Design-decision banners moved out of A's simulated OS; the review retains rationale.
- ABC/symbol keyboards use four rows; Ctrl/Alt/Super/Shift, Esc/Tab, caret keys, Fn and Desktop shortcuts remain. All four arrows are available on Fn. Existing focus and one-shot modifier state handling is retained.

### Second A refinement, 29 October 2026

Only A changed; B and C are untouched. The previous A is archived as
`omarchy-touch-review-a-pass1.html` (not included).

- Apps fill the screen: a thin app bar (name, Window, Back for Chromium) and the app body. No page title or captions.
- The dock steps aside inside apps and whenever the keyboard is up. A 48px bottom handle reveals it (tap, or swipe up for Home); a keyboard button sits beside the handle.
- Tapping a text field raises the keyboard for that field. Search raises it on arrival. One Hide control while typing (in the keyboard header).
- Page-level Back is one tap labelled “Back” everywhere; Escape still hides the keyboard first.
- Workspace window actions only appear once two apps are paired; Ratio is landscape-only.
- In-OS design notes and review levers were removed from A’s simulated screens. Wi-Fi “Simulate failure” now lives only in the staged Connecting state.
- Smaller polish: one-row Do not disturb / Clear all, icon dismiss, visible off tiles, consistent Power buttons, segmented Open/Save with highlighted selection and footer actions, side-by-side Wi-Fi and Sound layouts, lock date fixed, `-` and `/` on the ABC keyboard, portrait Home anchors the resume row low, status bar marks the open system layer.

### Text editing by gesture (A), 29 October 2026

A’s Text & context surface no longer shows Select / Copy / Cut / Paste buttons. Double-tap or long-press (500 ms) a word to select it and open a floating edit menu: Copy, Select sentence, Select all. Double-tap or long-press any A text field for Cut / Copy / Paste (with a selection) or Select / Select all / Paste (caret only). Password fields never offer Copy or Cut. Right-click and the Menu key / Shift+F10 open the same menu without touch; tap elsewhere or Escape dismisses it. Chromium’s mock page no longer has a “Select text” button. Selection handles are still visual only (not draggable). Verified in Chromium with mouse double-click, press-and-hold and emulated touch double-tap; not tested on the tablet’s touchscreen.

This remains a local mock, with no tablet changes or native integration claims.
The refined Home presentation was viewed at 728px and 375px with no console
errors. A narrow-width direction selector wrapping issue was corrected. This
is not a full visual audit of every screen or a physical touch test.

## Three coherent iterations

| Direction | Navigation / information hierarchy | Windowing strategy | Main tradeoff |
| --- | --- | --- | --- |
| A · Home & Dock | Home anchors app discovery; pinned grid, stable dock, search sheet and recent-app cards | Full-screen by default; deliberate two-app mode | Familiar to iPad users; more trips through Home |
| B · App Rail | Persistent labeled app rail; system inspector and master/detail lists | One main app pane; explicit two-slot pairing | Fast app switching; rail consumes portrait width |
| C · Workspace Deck | Named Work / Read / Personal spaces; task deck before individual apps | Workspace pairs, explicit pane focus, ratio and window actions | Preserves Linux task identity; more concepts to learn |

All three retain Omarchy’s dark solid surfaces, theme accents, visible Terminal, fixed window actions and keyboard shortcuts. They adapt familiar Home, dock, sidebar, sheet, Back and multitasking conventions without copying Apple’s visual materials. No blur, live app thumbnails, continuous animation or video backgrounds.

## Coverage: the same 18 surfaces in every direction

The selector exposes every surface; the expandable matrix has 54 direct jump buttons. Compare mode stacks three interactive previews at the same selected surface. Choosing a new surface synchronizes them; subsequent interactions are independent.

1. Lock / unlock: demo password, incorrect-password feedback, keyboard, explicit Unlock; hidden notification details.
2. Home / desktop and dock: return point, pinned / resume apps, system navigation.
3. App library: grid, grouped list, or workspace destination; launch controls.
4. Global search: app, file and settings results; scope filters and no-results state.
5. Recents: resume, close confirmation, empty state, split entry.
6. Workspaces / split view: named spaces, explicit pane focus, ratio, swap, single app, move and close.
7. Quick controls: Wi-Fi, Bluetooth, volume, brightness, rotation and routes to settings / power.
8. Notifications / DND: per-item dismissal, clear all, retained history and empty state.
9. Settings: categories, Display / Sound / Network / Keyboard / Accessibility / About detail, shell Back.
10. Wi-Fi: off, available, password error, connecting, cancel, completed connection, connection failure; open-network caution.
11. Keyboard: ABC, symbols, Fn/navigation, Desktop actions, one-shot Ctrl/Alt/Super/Shift, named target and explicit hide.
12. Terminal / app chrome: mock app input, native-app ownership, system window menu, terminal-aware shortcuts.
13. Files / picker: locations, folders, empty Downloads, selected file, Open / Save, filename and confirmation.
14. Selection / context / clipboard: illustrative selection handles, working select/copy/cut/paste buttons, mock clipboard and note destination.
15. OSD / media: live mock slider readouts, speaker volume, brightness, play/pause and track transport.
16. Power: Lock first, cancelable restart / shutdown confirmation; unavailable Sleep explained.
17. Permissions / error / offline / empty: window-sharing prompt, allow once / deny, retry and local-files recovery.
18. Accessibility / orientation / first use: larger shell text sample, higher contrast, optional handle-swipe practice, button alternatives, portrait adaptation.

Each direction has its own rendering and structural composition for every surface. Reusable controls and content are shared intentionally. This is not three color variants.

## Review interactions

Try **Home → Search → Terminal → Keys → type → Recents → Split view**. Type with a physical keyboard in mock fields, or with the rendered OSK. Nothing typed executes a command.

- Portrait / landscape uses 800 × 1280 / 1280 × 800 logical bounds, matching scale 1.5. Fit scales the entire preview; Full size retains logical target sizes and scrolls. Below 728px, Full size is the initial mode so controls are not silently shrunk. The outer workbench wraps down to 360px.
- A field tap names the destination above Keys. A key click inserts once at the caret. Shift affects one character. Ctrl/Alt/Super can combine for one shortcut and then clear. Changing keyboard page, app, orientation, focus target, hiding Keys or entering a confirmation clears modifiers.
- Terminal Ctrl+A means beginning of command. Ctrl+C is not presented as Copy; Super+C/V uses the terminal-aware mock clipboard route. Fn includes Esc, Tab, navigation and F1–F12. Shortcut examples not modeled in detail report a simulation instead of injecting OS events.
- **Shell Back / Escape** first cancels a dialog or hides Keys, then navigates a shell hierarchy. At Home, an app or the workspace canvas it does not traverse app history. Native app Back remains app-owned. Back cannot unlock the mock lock.
- Opening shell tools pauses app typing. Closing restores context with Keys hidden. Confirmations contain keyboard focus, make review/background controls inert, support Escape, and restore a named opener when available.
- To inspect network stages: choose Workshop, enter anything except `demo` for error; enter `demo` for Connecting; press Complete connection for Connected. No timer implies real hardware responded. Lock uses the same deliberately fake `demo` password. Never enter real credentials.
- The marked practice area recognizes a short upward swipe to Home. Labeled buttons cover all proposed gesture actions. No app-wide edge gesture interception is implemented; native gesture recognition is a feasibility item.
- Selection handles and app snapshots are visual examples. Use Select phrase / Context actions for actual mock selection. Handles are not draggable; app snapshots are not live. Mock folder depth and app contents are deliberately small.

Named native buttons, semantic inputs, visible focus, 48px shell and OSK controls, reduced-motion and forced-colors styles are included. A checkbox has a larger clickable label. These are implementation provisions, not a claim of a completed accessibility audit.

## Notes and privacy

Notes are keyed by direction and surface, in memory by default. The opt-in checkbox enables localStorage for **review notes only**; file-origin support depends on the browser. Existing saved notes are not read until opt-in. Turning it off removes that browser copy. Clear all removes saved and in-memory notes. Saved notes can be loaded after reload by opting in again.

Copy exports JSON. When the clipboard API is absent or blocked under file://, selectable text and a JSON download remain available. Exports exclude passwords, mock typed fields, mock clipboard, and device state. There is no telemetry, storage of credentials or runtime network access. Source links only open if clicked. Do not put private information in review notes.

## Grounding and reuse

Read-only project evidence: `PROJECT.md`, `ui/TOUCH-UI.md`, `ui/TOUCH-WORK.md`, `ui/rootfs/tablet-ui`, `ui/rootfs/TabletButton.qml`, `ui/wvkbd/tablet-layouts.h`, upstream shell component inventory, and `ui/evidence/tablet-touch-abc-live.png`. The screenshot was inspected for existing appearance and keyboard / launcher geometry. It was not a screenshot of this artifact.

| Shell responsibility | Candidate native reuse | Boundary / work still needed |
| --- | --- | --- |
| Panels, dock / rail, controls, dialogs | User-owned Omarchy Quickshell plugins; WidgetButton, Panel, PanelSlider, ToggleSwitch, ConfirmDialog, KeyboardPanel patterns; Qt Quick Controls | Checkout is newer than installed 4.0.6. Inspect installed properties before implementing. No packaged shell edits proposed. |
| App / workspace switching and pairs | Quickshell Hyprland client/workspace models; fixed Hyprland actions and existing launch bindings | Pair bookkeeping, supported layouts, minimum app widths and resize behavior need installed-compositor validation. No overview plugin assumed. |
| Keyboard / shortcuts | Existing local wvkbd fork and fixed tablet-ui helper | Preserve proven touch/key release logic and no-repeat protections. Prototype reservation is approximately 340px landscape with target header, exceeding current 260px; portrait uses up to 390px. Layout/reservation must be reconciled and measured before implementation. |
| Notifications / DND / clipboard | Native Omarchy / Quickshell services | Never scrape or log sensitive clipboard data; do not replace app selection behavior. |
| Networking | NetworkManager service/API | Policy, privilege, available hardware and connection state must be authoritative. |
| Speaker / media / OSD | PipeWire, MPRIS, existing native OSD patterns | Speaker output is verified in project notes. Microphone, headset and Bluetooth audio are unvalidated. |
| Open / Save and permissions | Existing GTK / Qt dialogs and XDG portals in participating applications | Non-portal apps keep their own dialogs. Shell cannot redraw arbitrary third-party application interiors. |
| Lock | Real session-lock implementation, e.g. Hyprlock if installed-compatible | Secure touch unlock, OSK on lock layer, PAM, event isolation and lock-failure recovery remain unresolved. HTML is not authentication. |

Rotation works in the real system; the preview toggle changes mock layout only. Reliable battery percentage/runtime, biometric authentication and safe suspend are not established. They are not invented in the designs. The browser remains CPU-rendered; H264 decoding is the validated video path. No performance benchmark is implied by this HTML.

## Official primary references

Checked 2026-10-28. Apple HIG is inspiration; Linux / toolkit sources describe possible implementation building blocks. The HTML contains the same directly linked, annotated bibliography.

- [Apple HIG: Gestures](https://developer.apple.com/design/human-interface-guidelines/gestures): familiar actions, button alternatives, gesture conflicts.
- [Apple HIG: Virtual keyboards](https://developer.apple.com/design/human-interface-guidelines/virtual-keyboards): task-appropriate input layouts.
- [Apple HIG: Multitasking](https://developer.apple.com/design/human-interface-guidelines/multitasking): continuity across app switching; not a claim to reproduce current iPad window management.
- [Apple HIG: Sidebars](https://developer.apple.com/design/human-interface-guidelines/sidebars) and [Tab bars](https://developer.apple.com/design/human-interface-guidelines/tab-bars): labeled navigation, stable hierarchy, adaptive structure.
- [Apple HIG: Accessibility](https://developer.apple.com/design/human-interface-guidelines/accessibility): readable text, contrast, alternate inputs, target sizing.
- [Omarchy source / manual](https://github.com/omacom/omarchy): native shell identity and reuse, version-sensitive APIs.
- [Quickshell layer-shell](https://quickshell.org/docs/v0.1.0/types/Quickshell.Wayland/WlrLayershell/) and [Hyprland types](https://quickshell.org/docs/v0.1.0/types/Quickshell.Hyprland/): panel placement, keyboard focus, client and workspace models.
- [Qt Quick Controls](https://doc.qt.io/qt-6/qtquickcontrols-index.html): established native control patterns.
- [wvkbd](https://github.com/jjsullivan5196/wvkbd): native Wayland OSK; this tablet uses a local modified fork.
- [Hyprlock](https://github.com/hyprwm/hyprlock): session-lock boundary; no assumption that its installed touch path is solved.
- [XDG FileChooser portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.FileChooser.html): OpenFile / SaveFile for participating apps.
- [NetworkManager API](https://networkmanager.dev/docs/api/latest/): authoritative connection management.
- [Lucide](https://github.com/lucide-icons/lucide): 55 embedded SVG symbols, ISC license included in the HTML. Aliases use audio-lines, text-cursor and ellipsis geometry. No icon download at runtime.

## Validation and honest limits

`validation.json` records **34 passing DOM/state checks**, including **108 rendered DOM combinations** (3 × 18 × 2), all four OSK pages, valid icon references and named buttons, no duplicate IDs, flow/state transitions, one-character taps, one-shot modifiers, caret preservation, confirmation focus / cancellation, notes isolation and clipboard fallback. Node + jsdom 26.1.0 ran locally under a file URL. This is meaningful interaction / structural verification, not a rendering engine or physical input test.

`contrast-tokens.json` is the initial review’s 36-pair token report. A uses refined colors now; that historical report is not a current contrast audit. Disabled opacity, actual computed styles and rendered pixels were not audited.

The Impeccable mechanical detector ran once. Its remaining no-side-inset warnings match the explicitly requested zero outer side padding. The initial system-font and heading-step flags were addressed; no second detector run was used as a visual claim.

**Unverified:** pixel-level contrast and clipping in B and C (A’s 18 surfaces and key flows were rendered in headless Chromium at both orientations, a visual pass rather than an audit); screen-reader behavior; physical finger comfort; actual Hyprland focus, native modifier/key release, touch event routing, orientation integration, installed component compatibility, auth security and hardware controls. The native keyboard’s previous tests do not validate this proposal. No preview tools, visual QA screenshots, web server, device commands, SSH, global package installs, services or publication were used.

Supporting `validate.cjs` can be rerun by temporarily installing its isolated dependency:

```sh
npm install --prefix design/touch-review/.validation-deps --no-save --ignore-scripts --no-audit --no-fund jsdom@26.1.0
node design/touch-review/validate.cjs
```

The HTML itself needs no npm packages. `embed-icons.py` is an optional authoring helper, not a runtime dependency. `NOTES.md` records authoring commands and scope. The local temporary validation dependencies were removed after verification.
