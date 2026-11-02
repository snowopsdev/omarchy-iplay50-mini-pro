import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons
import qs.Commons as Commons

// Touch lock screen for the tablet: a short PIN instead of a typed password.
//
// Replaces omarchy.lock (manifest `clonedFrom`; shell.json disables the
// original) and answers the same `lock` IPC target, so Omarchy's idle lock and
// lock-before-suspend (omarchy-system-sleep-lock) use it unchanged. Locking
// uses Quickshell's WlSessionLock (ext-session-lock): while locked Hyprland
// shows only this surface and sends it all input.
//
// The PIN is checked by /opt/omarchy-tablet/lock/tablet-lock-pin (scrypt hash
// in ~/.config/omarchy/tablet-lock.json, wrong tries rate-limited across
// restarts). Without a PIN `lock lock` refuses ("missing-pam"), as the stock
// lock does without PAM, so nothing can lock the tablet with no way back in.
//
// Screen: the power key runs tablet-power-key -> `tabletLock screenOff|wake`;
// /opt/omarchy-tablet/lock/tablet-display turns DPMS and the backlight off/on.
// While locked the screen turns off after 10 s untouched (30 s on the keypad).
//
// IPC `tabletLock`: lockNow (Home's Lock button; opens setup without a PIN),
// setup, screenOff, wake, enter PIN (types digits, for tests), state.
Item {
  id: root

  property var shell: null
  property string omarchyPath: ""
  property var manifest: null

  readonly property string pinTool: "/opt/omarchy-tablet/lock/tablet-lock-pin"
  readonly property string displayTool: "/opt/omarchy-tablet/lock/tablet-display"
  readonly property string hapticTool: "/opt/omarchy-tablet/haptics/tablet-haptic"
  readonly property string home: Quickshell.env("HOME")
  readonly property string pinFile: home + "/.config/omarchy/tablet-lock.json"
  readonly property string backgroundLink: home + "/.local/state/omarchy/current/background"

  // PIN store (tablet-lock-pin status)
  property bool pinSet: false
  property int pinLength: 0
  property bool statusKnown: false

  // Session lock
  property bool lockRequested: false
  property bool blankWhenSecure: false
  property bool screenIsOff: false         // turned off by blank(), until wake()
  property bool strandedLockResolved: false
  property string lastEvent: "init"
  property string lastEventAt: ""
  property string backgroundUrl: ""

  // Lock screen
  property string view: "cover"            // cover | pin
  property string entered: ""
  property string message: ""
  property bool messageIsError: false
  property bool checking: false
  property real waitUntil: 0               // epoch ms; keypad disabled until then
  property real now: Date.now()
  readonly property bool waiting: waitUntil > now
  signal wrongPin()

  function currentlyLocked() { return lockRequested || sessionLock.locked || sessionLock.secure }

  function logEvent(event) {
    lastEvent = event
    lastEventAt = new Date().toISOString()
    console.log("tablet lock " + lastEventAt + " " + event)
  }

  function run(args) { Quickshell.execDetached(args) }
  function buzz(ms) { run([hapticTool, String(ms)]) }

  // ------------------------------------------------------------- locking
  function beginLock(reason) {
    if (currentlyLocked()) return true
    resetEntry()
    view = "cover"
    lockRequested = true
    logEvent("lock-requested: " + reason)
    readlinkProc.running = true
    requestSessionLock()
    return true
  }

  function requestSessionLock() {
    if (!lockRequested || sessionLock.locked) return
    if (Quickshell.screens.length === 0) {
      if (!pendingLockTimer.running) pendingLockTimer.start()
      return
    }
    pendingLockTimer.stop()
    sessionLock.locked = true
    armIdle()
  }

  function finishUnlock() {
    if (!currentlyLocked()) return
    lockRequested = false
    blankWhenSecure = false
    pendingLockTimer.stop()
    idleTimer.stop()
    resetEntry()
    view = "cover"
    sessionLock.locked = false
    logEvent("unlocked")
  }

  // ------------------------------------------------------------- screen
  function screenOff() {
    if (pinSet && !currentlyLocked()) beginLock("power-key")
    if (currentlyLocked() && !sessionLock.secure) {
      // Turn the panel off only once the lock surface is up: with the
      // display off Hyprland renders nothing and the lock would never become
      // secure. The fallback timer covers a compositor that never confirms.
      blankWhenSecure = true
      blankFallback.restart()
      return "locking"
    }
    blank()
    return "ok"
  }

  function blank() {
    blankWhenSecure = false
    blankFallback.stop()
    idleTimer.stop()
    resetEntry()
    view = "cover"
    screenIsOff = true
    run([displayTool, "off"])
  }

  function wake() {
    run([displayTool, "on"])
    woke()
    return "ok"
  }

  function woke() {
    screenIsOff = false
    if (currentlyLocked()) { resetEntry(); view = "cover"; armIdle() }
  }

  function armIdle() {
    if (!currentlyLocked()) return
    idleTimer.interval = view === "pin" ? 30000 : 10000
    idleTimer.restart()
  }

  function poke() { armIdle() }

  // ------------------------------------------------------------- keypad
  function resetEntry() {
    entered = ""
    message = ""
    messageIsError = false
  }

  function showPin() {
    if (!pinSet) {
      // Only reachable when the PIN file was removed while locked, which
      // takes the user's own files (or root): nothing left to check.
      finishUnlock()
      return
    }
    view = "pin"
    if (waiting) updateWaitMessage()
    armIdle()
  }

  function showCover() {
    resetEntry()
    view = "cover"
    armIdle()
  }

  function pressDigit(d) {
    poke()
    if (checking || waiting || entered.length >= 8) return
    if (messageIsError) { message = ""; messageIsError = false }
    entered += d
    if (pinLength > 0 && entered.length === pinLength) submitPin()
  }

  function eraseDigit() {
    poke()
    if (!checking) entered = entered.slice(0, -1)
  }

  function submitPin() {
    if (checking || entered.length < 4) return
    checking = true
    unlockCall.input = entered + "\n"
    unlockCall.running = true
  }

  function updateWaitMessage() {
    var s = Math.ceil((waitUntil - Date.now()) / 1000)
    message = s > 0 ? "Too many tries. Wait " + (s >= 60 ? Math.ceil(s / 60) + " min" : s + " s") : ""
    messageIsError = s > 0
  }

  function unlockReply(r) {
    checking = false
    if (!currentlyLocked()) return
    if (r.result === "ok" || r.result === "no-pin") {
      finishUnlock()
      return
    }
    entered = ""
    if (r.wait > 0) {
      waitUntil = Date.now() + r.wait * 1000
      now = Date.now()
      updateWaitMessage()
    } else if (r.result === "wrong") {
      message = "Wrong PIN"
      messageIsError = true
    } else {
      message = "Couldn't check the PIN"
      messageIsError = true
    }
    if (r.result !== "wait") { wrongPin(); buzz(80) }
  }

  // ------------------------------------------------------------- setup
  // Steps: "current" (prove the old PIN) -> "new" -> "confirm" -> "done".
  property bool setupOpen: false
  property string setupStep: ""
  property string setupEntered: ""
  property string setupMessage: ""
  property bool setupError: false
  property bool setupBusy: false
  property string setupCurrent: ""
  property string setupFirst: ""
  signal setupWrong()

  function openSetup() {
    if (currentlyLocked()) return "locked"
    setupCurrent = ""
    setupFirst = ""
    setupEntered = ""
    setupError = false
    setupMessage = ""
    setupStep = pinSet ? "current" : "new"
    setupOpen = true
    // The touch keyboard sits in the overlay layer too and would cover the keys.
    run(["/opt/omarchy-tablet/ui/tablet-ui", "keyboard-hide"])
    return "ok"
  }

  function closeSetup() {
    setupOpen = false
    setupCurrent = ""
    setupFirst = ""
    setupEntered = ""
    setupBusy = false
  }

  function setupDigit(d) {
    if (setupBusy || setupEntered.length >= 8 || setupStep === "done") return
    if (setupError) { setupMessage = ""; setupError = false }
    setupEntered += d
    if (setupStep === "current" && setupEntered.length === pinLength) setupNext()
    else if (setupStep === "confirm" && setupEntered.length === setupFirst.length) setupNext()
  }

  function setupFail(text) {
    setupMessage = text
    setupError = true
    setupEntered = ""
    setupWrong()
    buzz(80)
  }

  function setupNext() {
    if (setupBusy) return
    if (setupStep === "current") {
      setupBusy = true
      setupCall.mode = "verify"
      setupCall.command = [pinTool, "check"]
      setupCall.input = setupEntered + "\n"
      setupCall.running = true
    } else if (setupStep === "new") {
      if (setupEntered.length < 4) { setupFail("Use 4 to 8 numbers"); return }
      setupFirst = setupEntered
      setupEntered = ""
      setupMessage = ""
      setupStep = "confirm"
    } else if (setupStep === "confirm") {
      if (setupEntered !== setupFirst) {
        setupFirst = ""
        setupStep = "new"
        setupFail("The PINs didn't match. Start again")
        return
      }
      setupBusy = true
      setupCall.mode = "set"
      setupCall.command = [pinTool, "set"]
      setupCall.input = setupCurrent + "\n" + setupFirst + "\n"
      setupCall.running = true
    }
  }

  function removePin() {
    if (setupBusy || !pinSet) return
    setupBusy = true
    setupCall.mode = "clear"
    setupCall.command = [pinTool, "clear"]
    setupCall.input = setupCurrent + "\n"
    setupCall.running = true
  }

  function setupReply(mode, r) {
    setupBusy = false
    if (r.result === "wait") {
      setupFail("Too many tries. Wait " + r.wait + " s")
      return
    }
    if (mode === "verify") {
      if (r.result !== "ok") { setupFail(r.wait > 0 ? "Wrong PIN. Wait " + r.wait + " s" : "Wrong PIN"); return }
      setupCurrent = setupEntered
      setupEntered = ""
      setupMessage = ""
      setupStep = "new"
      return
    }
    if (r.result !== "ok") {
      setupStep = pinSet ? "current" : "new"
      setupCurrent = ""
      setupFirst = ""
      setupFail("Couldn't save the PIN")
      return
    }
    setupStep = "done"
    setupEntered = ""
    setupError = false
    setupMessage = mode === "clear" ? "PIN removed. The tablet no longer locks."
                                    : "PIN saved. Press the power key or Lock to lock."
    setupCurrent = ""
    setupFirst = ""
    statusCall.running = true
    setupDone.restart()
  }

  Timer { id: setupDone; interval: 1800; onTriggered: root.closeSetup() }

  // ------------------------------------------------------------- helpers
  component PinCall: Process {
    id: call
    property string input: ""
    signal replied(var r)
    stdinEnabled: true
    onStarted: { write(input); input = "" }
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var r
        try { r = JSON.parse(String(text || "").trim()) } catch (e) { r = { result: "error" } }
        call.replied(r)
      }
    }
  }

  PinCall {
    id: statusCall
    command: [root.pinTool, "status"]
    onReplied: function(r) {
      if (r.result === "error") return
      root.pinSet = !!r.pin
      root.pinLength = r.length || 0
      root.statusKnown = true
      if (r.wait > 0) root.waitUntil = Date.now() + r.wait * 1000
      root.checkStrandedLock()
    }
  }

  PinCall {
    id: unlockCall
    command: [root.pinTool, "check"]
    onReplied: function(r) { root.unlockReply(r) }
  }

  PinCall {
    id: setupCall
    property string mode: ""
    onReplied: function(r) { root.setupReply(mode, r) }
  }

  // Follow the PIN file so a PIN set elsewhere (or removed by root) counts.
  FileView {
    path: root.pinFile
    watchChanges: true
    printErrors: false
    onLoaded: statusCall.running = true
    onLoadFailed: statusCall.running = true
    onFileChanged: reload()
  }

  // The wallpaper, blurred once per wallpaper into ~/.cache/tablet-lock (a
  // still image: no blur shader runs while the lock screen is up).
  Process {
    id: readlinkProc
    command: ["bash", "-c", "src=$(readlink -f \"$1\") && [[ -f $src ]] || exit 0\n"
      + "dir=$HOME/.cache/tablet-lock\n"
      + "out=$dir/bg-$(stat -c '%n %Y %s' \"$src\" | md5sum | cut -c1-12).jpg\n"
      + "if [[ ! -s $out ]]; then mkdir -p \"$dir\"; rm -f \"$dir\"/bg-*.jpg\n"
      + "  magick -define jpeg:size=1280x1280 \"$src\" -resize '480x480^' -blur 0x10 -quality 88 \"$out.tmp\" && mv \"$out.tmp\" \"$out\" || out=$src\n"
      + "fi\necho \"$out\"", "lock-background", root.backgroundLink]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var path = String(text || "").trim()
        root.backgroundUrl = path ? "file://" + path.split("/").map(encodeURIComponent).join("/") : ""
      }
    }
  }

  Component.onCompleted: readlinkProc.running = true


  // A lock left behind by a shell that exited while locked (ext-session-lock
  // outlives its client; Hyprland then shows its "lockscreen crashed" page).
  // Take it over so the PIN can open it again. Exit 2: cannot tell yet.
  function checkStrandedLock() {
    if (strandedLockResolved || strandedProc.running || !statusKnown) return
    if (currentlyLocked()) { strandedLockResolved = true; return }
    strandedProc.running = true
  }

  Process {
    id: strandedProc
    command: ["bash", "-c", "omarchy-hyprland-session-lock-stranded \"$PPID\""]
    onExited: function(code) {
      if (code === 2) return
      root.strandedLockResolved = true
      if (code === 0 && !root.currentlyLocked()) root.beginLock("stranded")
    }
  }

  Timer {
    interval: 500
    repeat: true
    property int left: 20
    running: !root.strandedLockResolved && left > 0
    onTriggered: { left -= 1; root.checkStrandedLock() }
  }

  Timer { id: pendingLockTimer; interval: 100; repeat: true; onTriggered: root.requestSessionLock() }
  // Hyprland (a key press) and Omarchy's idle wake turn DPMS back on without
  // the backlight that tablet-display turned off. On any input while the
  // screen is off, `tablet-display sync` finishes such a wake. The 2 s
  // timeout keeps the power key press that turned the screen off out of it.
  IdleMonitor {
    enabled: root.screenIsOff
    respectInhibitors: false
    timeout: 2
    onIsIdleChanged: if (!isIdle && !displaySync.running) displaySync.running = true
  }
  Process {
    id: displaySync
    command: [root.displayTool, "sync"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: if (String(text).trim() === "on" && root.screenIsOff) root.woke()
    }
  }

  Timer { id: blankFallback; interval: 2000; onTriggered: if (root.blankWhenSecure) root.blank() }
  Timer {
    id: idleTimer
    onTriggered: if (root.currentlyLocked() && !root.checking) root.blank()
  }
  Timer {
    interval: 1000
    repeat: true
    running: root.waitUntil > root.now
    onTriggered: {
      root.now = Date.now()
      if (root.currentlyLocked() && root.view === "pin") root.updateWaitMessage()
    }
  }

  Connections {
    target: Quickshell
    function onScreensChanged() { root.requestSessionLock() }
  }

  WlSessionLock {
    id: sessionLock
    locked: false

    onSecureStateChanged: {
      root.logEvent("secure=" + secure)
      if (secure && root.blankWhenSecure) root.blank()
    }

    onLockStateChanged: {
      root.logEvent("session-locked=" + locked)
      // The compositor ended the lock (or refused it): drop our side too.
      if (!locked && root.lockRequested) {
        root.lockRequested = false
        root.blankWhenSecure = false
        idleTimer.stop()
        root.resetEntry()
      }
    }

    WlSessionLockSurface {
      color: "black"
      LockView {
        anchors.fill: parent
        lock: root
      }
    }
  }

  // PIN setup sheet, opened from Home (unlocked session only).
  PanelWindow {
    id: setupWindow
    visible: root.setupOpen
    anchors { top: true; bottom: true; left: true; right: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "tablet-lock-setup"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

    readonly property color fg: Commons.Color.foreground

    Rectangle { anchors.fill: parent; color: Commons.Color.background }
    MouseArea { anchors.fill: parent }   // swallow touches outside the keys

    Text {
      anchors.top: parent.top
      anchors.left: parent.left
      anchors.margins: 22
      text: String.fromCodePoint(0xF0156)
      color: setupWindow.fg
      font.family: Style.font.family
      font.pixelSize: 30
      TapHandler { onTapped: root.closeSetup() }
    }

    Column {
      anchors.centerIn: parent
      spacing: 18

      Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: ({ "current": "Enter your current PIN", "new": "Choose a lock PIN", "confirm": "Enter it again", "done": "Lock PIN" })[root.setupStep] || ""
        color: setupWindow.fg
        font.family: Style.font.family
        font.pixelSize: 24
        font.weight: Font.DemiBold
      }
      Text {
        anchors.horizontalCenter: parent.horizontalCenter
        visible: root.setupStep === "new"
        text: "4 to 8 numbers. It unlocks the screen."
        color: Qt.rgba(setupWindow.fg.r, setupWindow.fg.g, setupWindow.fg.b, 0.7)
        font.family: Style.font.family
        font.pixelSize: 16
      }

      PinPad {
        id: setupPad
        anchors.horizontalCenter: parent.horizontalCenter
        visible: root.setupStep !== "done"
        fg: setupWindow.fg
        entered: root.setupEntered.length
        length: root.setupStep === "current" ? root.pinLength
              : (root.setupStep === "confirm" ? root.setupFirst.length : 0)
        message: root.setupMessage
        error: root.setupError
        busy: root.setupBusy
        leftLabel: "Cancel"
        okLabel: root.setupStep === "new" ? "Next" : ""
        okEnabled: root.setupEntered.length >= 4
        onDigit: function(d) { root.setupDigit(d) }
        onErase: root.setupEntered = root.setupEntered.slice(0, -1)
        onOkPressed: root.setupNext()
        onLeftPressed: root.closeSetup()
      }

      Text {
        anchors.horizontalCenter: parent.horizontalCenter
        visible: root.setupStep === "new" && root.pinSet && root.setupCurrent !== ""
        text: "Remove PIN"
        color: Qt.lighter(Commons.Color.urgent, 1.3)
        font.family: Style.font.family
        font.pixelSize: 17
        TapHandler { onTapped: root.removePin() }
      }

      Text {
        anchors.horizontalCenter: parent.horizontalCenter
        visible: root.setupStep === "done"
        text: root.setupMessage
        color: setupWindow.fg
        font.family: Style.font.family
        font.pixelSize: 19
      }
    }

    Connections {
      target: root
      function onSetupWrong() { setupPad.shake() }
    }
  }

  // ------------------------------------------------------------- IPC
  IpcHandler {
    target: "lock"

    function lock(): string {
      if (!root.pinSet) return "missing-pam"
      root.beginLock("ipc")
      return "ok"
    }

    function isLocked(): string { return root.currentlyLocked() ? "true" : "false" }

    function status(): string {
      return JSON.stringify({
        locked: root.currentlyLocked(),
        requested: root.lockRequested,
        sessionLocked: sessionLock.locked,
        secure: sessionLock.secure,
        realScreens: Quickshell.screens.length,
        passwordPam: root.pinSet,
        fingerprint: false,
        authenticating: root.checking,
        lastEvent: root.lastEvent,
        lastEventAt: root.lastEventAt
      })
    }

    function preview(): string { return "unsupported" }
    function hidePreview(): string { return "ok" }
  }

  IpcHandler {
    target: "tabletLock"

    function lockNow(): string {
      if (!root.pinSet) return root.openSetup() === "ok" ? "setup" : "locked"
      root.beginLock("home")
      return "ok"
    }
    function setup(): string { return root.openSetup() }
    function screenOff(): string { return root.screenOff() }
    function wake(): string { return root.wake() }
    function enter(pin: string): string {
      if (!root.currentlyLocked()) return "not-locked"
      root.showPin()
      for (var i = 0; i < pin.length; i++) root.pressDigit(pin[i])
      return "ok"
    }
    function state(): string {
      return JSON.stringify({
        pin: root.pinSet, length: root.pinLength, locked: root.currentlyLocked(),
        secure: sessionLock.secure, view: root.view, entered: root.entered.length,
        checking: root.checking, message: root.message,
        wait: Math.max(0, Math.ceil((root.waitUntil - Date.now()) / 1000)),
        setup: root.setupOpen ? root.setupStep : "", screenOff: root.screenIsOff,
        lastEvent: root.lastEvent
      })
    }
  }
}
