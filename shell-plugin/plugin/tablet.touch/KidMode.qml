import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import Quickshell.Wayland

// Kid Mode: a switchable, parent-PIN-protected kiosk on top of the touch shell.
//
// While active (flag file ~/.local/state/tablet-touch/kid-mode, survives reboot):
// - big colourful tiles of the allow-listed apps on their own "kids" workspace,
//   each app maximised; a Home pill at the bottom returns to the tiles;
// - Omarchy's bar is hidden, Home/dock/handle/Open apps are off, the touch
//   keyboard uses kid pages (no Super/Ctrl/Alt) and tablet-ui refuses launches;
// - any window that is not an allowed app is closed as soon as it opens;
// - the lock button asks for the parent PIN: change apps, change PIN, leave.
// Settings: ~/.config/omarchy/tablet-kids.json (allow list, learned window
// classes, salted PIN hash). The hash keeps a child out, not a determined adult.
Item {
  id: kid

  required property var touch          // TouchShell root
  readonly property string home: Quickshell.env("HOME")
  readonly property string flagPath: home + "/.local/state/tablet-touch/kid-mode"
  readonly property string configPath: home + "/.config/omarchy/tablet-kids.json"
  readonly property string workspace: "kids"

  property bool active: false
  property var config: ({ allow: [], classes: ({}), pinSalt: "", pinHash: "" })
  property bool homeRequested: true
  property var pendingLaunch: null      // { id, until } — learn the next window's class

  // Overlay views: "" | "pin" (unlock) | "parent" | "setup-pin" | "setup-confirm" | "apps"
  property string view: ""
  property string pin: ""
  property string pinFirst: ""
  property string message: ""
  property int failures: 0
  property real lockedUntil: 0
  property bool enterAfterApps: false

  readonly property bool hasPin: !!config.pinHash
  readonly property var allowed: config.allow || []
  readonly property var allowedEntries: {
    var out = []
    var apps = kid.touch.apps
    for (var i = 0; i < apps.length; i++)
      if (allowed.indexOf(apps[i].id) !== -1) out.push(apps[i])
    return out
  }
  readonly property bool appOpen: active && touch.workspaceWindows.length > 0
  readonly property bool homeVisible: active && (homeRequested || !appOpen)
  readonly property bool stripVisible: active && !homeVisible && view === ""
  readonly property bool overlayVisible: view !== ""

  readonly property var tileColors: ["#ffb4a2", "#ffd166", "#9be7a2", "#8ecae6", "#cdb4ff", "#ffafcc"]
  readonly property color ink: "#1b1b2f"
  readonly property color sky: "#1f2a44"

  // ------------------------------------------------------------- storage
  FileView {
    id: flagFile
    path: kid.flagPath
    printErrors: false
    onLoaded: { kid.active = true; kid.applyActive() }
    onLoadFailed: kid.active = false
  }

  FileView {
    id: configFile
    path: kid.configPath
    printErrors: false
    atomicWrites: true
    onLoaded: {
      try {
        var c = JSON.parse(text())
        if (!Array.isArray(c.allow)) c.allow = []
        if (!c.classes || typeof c.classes !== "object") c.classes = ({})
        kid.config = c
      } catch (e) { console.warn("tablet-kids.json unreadable:", e) }
    }
  }

  function save() {
    configFile.setText(JSON.stringify(config, null, 2) + "\n")
    config = JSON.parse(JSON.stringify(config))   // notify bindings
  }

  function run(script) {
    Quickshell.execDetached(["bash", "-c", script])
  }

  // ------------------------------------------------------------- PIN
  function hashPin(salt, value) { return Qt.md5(salt + ":" + value) }

  function setPin(value) {
    var c = config
    c.pinSalt = Math.random().toString(36).slice(2) + Date.now().toString(36)
    c.pinHash = hashPin(c.pinSalt, value)
    config = c
    save()
  }

  function pinMatches(value) { return hasPin && hashPin(config.pinSalt, value) === config.pinHash }

  function pressDigit(d) {
    if (Date.now() < lockedUntil) return
    if (pin.length < 8) pin += d
    message = ""
  }

  function submitPin() {
    if (view === "pin") {
      if (Date.now() < lockedUntil) return
      if (pinMatches(pin)) { failures = 0; pin = ""; view = "parent"; return }
      failures++
      pin = ""
      if (failures >= 5) {
        lockedUntil = Date.now() + 60000
        failures = 0
        message = "Too many tries. Wait a minute."
        lockTimer.restart()
      } else message = "That's not it. Try again."
    } else if (view === "setup-pin") {
      if (pin.length < 4) { message = "Use at least 4 numbers."; return }
      pinFirst = pin; pin = ""; view = "setup-confirm"; message = ""
    } else if (view === "setup-confirm") {
      if (pin !== pinFirst) { pin = ""; pinFirst = ""; view = "setup-pin"; message = "They didn't match. Start again."; return }
      setPin(pin); pin = ""; pinFirst = ""
      view = "apps"
    }
  }

  Timer { id: lockTimer; interval: 60000; onTriggered: kid.message = "" }

  // ------------------------------------------------------------- mode
  // Entry point from the normal Home. First use: set a PIN, choose apps.
  function start() {
    touch.close()
    if (!hasPin) { pin = ""; message = ""; enterAfterApps = true; view = "setup-pin"; return }
    if (allowed.length === 0) { enterAfterApps = true; view = "apps"; return }
    enter()
  }

  function enter() {
    view = ""
    enterAfterApps = false
    homeRequested = true
    // Hide the bar only if it was showing; remember that for leave().
    run('set -e; d="$HOME/.local/state/tablet-touch"; t="$HOME/.local/state/omarchy/toggles"; '
        + 'mkdir -p "$d" "$t"; if [ -e "$t/bar-off" ]; then echo keep-bar > "$d/kid-mode"; '
        + 'else echo restore-bar > "$d/kid-mode"; touch "$t/bar-off"; fi; '
        + 'omarchy-shell omarchy.bar syncHidden >/dev/null 2>&1 || true; '
        + '/opt/omarchy-tablet/ui/tablet-ui keyboard-restart')
    active = true
    applyActive()
  }

  function leave() {
    view = ""
    active = false
    pendingLaunch = null
    run('d="$HOME/.local/state/tablet-touch"; '
        + 'if grep -q restore-bar "$d/kid-mode" 2>/dev/null; then rm -f "$HOME/.local/state/omarchy/toggles/bar-off"; fi; '
        + 'rm -f "$d/kid-mode"; omarchy-shell omarchy.bar syncHidden >/dev/null 2>&1 || true; '
        + '/opt/omarchy-tablet/ui/tablet-ui keyboard-restart')
    touch.hypr('hl.dsp.focus({ workspace = "1" })')
  }

  // Also runs at shell start when the flag survived a reboot.
  function applyActive() {
    if (!active) return
    touch.close()
    touch.hypr('hl.dsp.focus({ workspace = "name:' + workspace + '" })')
  }

  // ------------------------------------------------------------- apps
  function classesFor(entry) {
    var out = []
    if (!entry) return out
    if (entry.startupClass) out.push(String(entry.startupClass).toLowerCase())
    out.push(String(entry.id).toLowerCase())
    var dot = entry.id.lastIndexOf(".")
    if (dot !== -1) out.push(entry.id.slice(dot + 1).toLowerCase())
    var learned = config.classes[entry.id] || []
    for (var i = 0; i < learned.length; i++) out.push(String(learned[i]).toLowerCase())
    return out
  }

  function entryForClass(cls) {
    var c = String(cls || "").toLowerCase()
    for (var i = 0; i < allowedEntries.length; i++)
      if (classesFor(allowedEntries[i]).indexOf(c) !== -1) return allowedEntries[i]
    return null
  }

  function toggleAllowed(id) {
    var c = config
    var list = (c.allow || []).slice()
    var at = list.indexOf(id)
    if (at === -1) list.push(id); else list.splice(at, 1)
    c.allow = list
    config = c
    save()
  }

  function open(entry) {
    // Already running on the kids workspace: bring it back.
    var wins = touch.workspaceWindows
    for (var i = 0; i < wins.length; i++) {
      if (classesFor(entry).indexOf(touch.windowClass(wins[i]).toLowerCase()) !== -1) {
        touch.hypr('hl.dsp.focus({ window = "address:' + touch.address(wins[i]) + '" })')
        homeRequested = false
        return
      }
    }
    pendingLaunch = { id: entry.id, until: Date.now() + 20000 }
    touch.run(["uwsm-app", "--", "gtk-launch", entry.id + ".desktop"])
    homeRequested = false
  }

  // Hyprland openwindow>>ADDRESS,WORKSPACE,CLASS,TITLE while active.
  function windowOpened(data) {
    if (!active) return
    var parts = String(data).split(",")
    var addr = "0x" + parts[0]
    var cls = parts.length > 2 ? parts[2] : ""
    var entry = entryForClass(cls)
    if (!entry && pendingLaunch && Date.now() < pendingLaunch.until) {
      // First window after a tile tap: remember its class for that app.
      var c = config
      var known = (c.classes[pendingLaunch.id] || []).slice()
      if (cls && known.indexOf(cls) === -1) known.push(cls)
      c.classes[pendingLaunch.id] = known
      config = c
      save()
      entry = entryForClass(cls)
    }
    if (!entry) {
      touch.hypr('hl.dsp.window.close({ window = "address:' + addr + '" })')
      return
    }
    pendingLaunch = null
    touch.hypr('hl.dsp.window.move({ workspace = "name:' + workspace + '", window = "address:' + addr + '" })')
    fill(addr)
    homeRequested = false
  }

  // One app fills the screen: tile it (Omarchy floats some, e.g. the
  // calculator) and set maximised. "set" is idempotent, unlike the toggle.
  function fill(addr) {
    touch.hypr('hl.dsp.window.float({ action = "disable", window = "address:' + addr + '" })')
    touch.hypr('hl.dsp.window.fullscreen({ mode = "maximized", action = "set", window = "address:' + addr + '" })')
  }

  // Re-apply after any window change (a window leaving can drop the state).
  function scheduleNormalize() { if (active) normalizeTimer.restart() }
  Timer {
    id: normalizeTimer
    interval: 400
    onTriggered: {
      var wins = kid.touch.workspaceWindows
      for (var i = 0; i < wins.length; i++) kid.fill(kid.touch.address(wins[i]))
    }
  }

  SystemClock { id: kidClock; precision: SystemClock.Minutes }

  // ============================================================ widgets
  component Button: Rectangle {
    id: b
    signal activated()
    property string label: ""
    property color fill: "#ffffff"
    property color textColor: kid.ink
    property int textSize: 26
    radius: 22
    color: tap.pressed ? Qt.darker(fill, 1.15) : fill
    scale: tap.pressed ? 0.96 : 1
    Behavior on scale { NumberAnimation { duration: 80 } }
    Text {
      anchors.centerIn: parent
      text: b.label
      color: b.textColor
      font.family: kid.touch.fontFamily
      font.pixelSize: b.textSize
      font.weight: Font.Bold
    }
    TapHandler { id: tap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: b.activated() }
  }

  // ================================================================ Kid Home
  PanelWindow {
    id: homeWindow
    screen: Quickshell.screens.length > 0 ? Quickshell.screens[0] : null
    visible: kid.touch.mapAllowed
    anchors { top: true; left: true; right: true; bottom: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "tablet-kids"
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    // Kept mapped like the other shell surfaces; input only while shown.
    mask: Region { width: kid.homeVisible ? homeWindow.width : 0; height: kid.homeVisible ? homeWindow.height : 0 }

    Item {
      anchors.fill: parent
      visible: kid.homeVisible

      Rectangle {
        anchors.fill: parent
        gradient: Gradient {
          GradientStop { position: 0; color: "#24345c" }
          GradientStop { position: 1; color: "#1a2238" }
        }
      }

      Text {
        id: kidTime
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 36
        text: Qt.formatTime(kidClock.date, "h:mm")
        color: "#ffffff"
        font.family: kid.touch.fontFamily
        font.pixelSize: 56
        font.weight: Font.Bold
      }

      // Parent lock: small and quiet; the PIN does the protecting.
      Rectangle {
        id: lockButton
        width: 64; height: 64; radius: 32
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 28
        color: lockTap.pressed ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(1, 1, 1, 0.08)
        Text {
          anchors.centerIn: parent
          text: String.fromCodePoint(0xF033E)
          color: Qt.rgba(1, 1, 1, 0.55)
          font.family: kid.touch.fontFamily
          font.pixelSize: 28
        }
        TapHandler {
          id: lockTap
          gesturePolicy: TapHandler.ReleaseWithinBounds
          onTapped: { kid.pin = ""; kid.message = ""; kid.view = "pin" }
        }
      }

      Text {
        visible: kid.allowedEntries.length === 0
        anchors.centerIn: parent
        text: "No apps yet — a grown-up can add some."
        color: "#ffffff"
        font.family: kid.touch.fontFamily
        font.pixelSize: 28
      }

      GridView {
        id: kidGrid
        anchors.top: kidTime.bottom
        anchors.topMargin: 28
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        readonly property int columns: Math.max(2, Math.min(5, Math.floor((homeWindow.width - 48) / cellWidth)))
        // Few apps: shrink to fit so the tiles sit centred.
        width: Math.max(1, Math.min(columns, count)) * cellWidth
        cellWidth: 232
        cellHeight: 232
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: kid.allowedEntries
        delegate: Item {
          id: tileCell
          required property var modelData
          required property int index
          width: kidGrid.cellWidth
          height: kidGrid.cellHeight
          Rectangle {
            id: tile
            anchors.fill: parent
            anchors.margins: 14
            radius: 36
            color: kid.tileColors[tileCell.index % kid.tileColors.length]
            scale: tileTap.pressed ? 0.94 : 1
            Behavior on scale { NumberAnimation { duration: 90 } }
            Image {
              anchors.horizontalCenter: parent.horizontalCenter
              anchors.top: parent.top
              anchors.topMargin: 26
              width: 104; height: 104
              source: kid.touch.iconFor(tileCell.modelData, "")
              sourceSize.width: 208
              sourceSize.height: 208
              fillMode: Image.PreserveAspectFit
              asynchronous: true
            }
            Text {
              anchors.bottom: parent.bottom
              anchors.bottomMargin: 18
              anchors.horizontalCenter: parent.horizontalCenter
              width: parent.width - 20
              horizontalAlignment: Text.AlignHCenter
              text: tileCell.modelData.name
              color: kid.ink
              font.family: kid.touch.fontFamily
              font.pixelSize: 22
              font.weight: Font.Bold
              elide: Text.ElideRight
            }
            TapHandler {
              id: tileTap
              gesturePolicy: TapHandler.ReleaseWithinBounds
              onTapped: kid.open(tileCell.modelData)
            }
          }
        }
      }
    }
  }

  // ================================================================ Home pill
  PanelWindow {
    id: stripWindow
    screen: Quickshell.screens.length > 0 ? Quickshell.screens[0] : null
    visible: kid.stripVisible && kid.touch.mapAllowed
    anchors { left: true; right: true; bottom: true }
    implicitHeight: 64
    color: "#1a2238"
    exclusionMode: ExclusionMode.Auto
    WlrLayershell.namespace: "tablet-kids-strip"
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

    Button {
      anchors.centerIn: parent
      width: 220; height: 50; radius: 25
      label: String.fromCodePoint(0xF02DC) + "  Home"
      fill: "#ffd166"
      textSize: 22
      onActivated: kid.homeRequested = true
    }
  }

  // ======================================================= PIN / parent / apps
  PanelWindow {
    id: overlayWindow
    screen: Quickshell.screens.length > 0 ? Quickshell.screens[0] : null
    visible: kid.touch.mapAllowed
    anchors { top: true; left: true; right: true; bottom: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "tablet-kids-parent"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    mask: Region { width: kid.overlayVisible ? overlayWindow.width : 0; height: kid.overlayVisible ? overlayWindow.height : 0 }

    Item {
      anchors.fill: parent
      visible: kid.overlayVisible

      Rectangle {
        anchors.fill: parent
        color: "#121726"
        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds }   // swallow taps
      }

      // ---------------------------------------------------------- PIN pad
      Column {
        visible: kid.view === "pin" || kid.view === "setup-pin" || kid.view === "setup-confirm"
        anchors.centerIn: parent
        spacing: 18

        Text {
          anchors.horizontalCenter: parent.horizontalCenter
          text: kid.view === "pin" ? "Grown-ups only"
              : kid.view === "setup-pin" ? "Choose a parent PIN" : "Enter the PIN again"
          color: "#ffffff"
          font.family: kid.touch.fontFamily
          font.pixelSize: 30
          font.weight: Font.Bold
        }
        Row {
          anchors.horizontalCenter: parent.horizontalCenter
          spacing: 14
          height: 24
          Repeater {
            model: Math.max(4, kid.pin.length)
            delegate: Rectangle {
              required property int index
              width: 20; height: 20; radius: 10
              color: index < kid.pin.length ? "#ffd166" : Qt.rgba(1, 1, 1, 0.2)
            }
          }
        }
        Text {
          anchors.horizontalCenter: parent.horizontalCenter
          height: 24
          text: kid.message
          color: "#ffafcc"
          font.family: kid.touch.fontFamily
          font.pixelSize: 18
        }
        Grid {
          anchors.horizontalCenter: parent.horizontalCenter
          columns: 3
          spacing: 12
          Repeater {
            model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "⌫", "0", "OK"]
            delegate: Button {
              required property string modelData
              width: 104; height: 76; radius: 20
              label: modelData
              fill: modelData === "OK" ? "#9be7a2" : (modelData === "⌫" ? "#cdb4ff" : "#ffffff")
              onActivated: {
                if (modelData === "OK") kid.submitPin()
                else if (modelData === "⌫") kid.pin = kid.pin.slice(0, -1)
                else kid.pressDigit(modelData)
              }
            }
          }
        }
        Button {
          anchors.horizontalCenter: parent.horizontalCenter
          width: 200; height: 56
          label: "Cancel"
          fill: Qt.rgba(1, 1, 1, 0.12)
          textColor: "#ffffff"
          textSize: 20
          onActivated: { kid.pin = ""; kid.pinFirst = ""; kid.message = ""; kid.view = ""; kid.enterAfterApps = false }
        }
      }

      // ---------------------------------------------------------- parent menu
      Column {
        visible: kid.view === "parent"
        anchors.centerIn: parent
        spacing: 16
        Text {
          anchors.horizontalCenter: parent.horizontalCenter
          text: "Parent settings"
          color: "#ffffff"
          font.family: kid.touch.fontFamily
          font.pixelSize: 30
          font.weight: Font.Bold
        }
        Button { width: 360; height: 68; label: "Choose apps"; fill: "#8ecae6"; onActivated: kid.view = "apps" }
        Button { width: 360; height: 68; label: "Change PIN"; fill: "#cdb4ff"; onActivated: { kid.pin = ""; kid.message = ""; kid.view = "setup-pin" } }
        Button { width: 360; height: 68; label: "Leave Kid Mode"; fill: "#ffb4a2"; onActivated: kid.leave() }
        Button { width: 360; height: 68; label: "Back"; fill: "#ffffff"; onActivated: kid.view = "" }
      }

      // ---------------------------------------------------------- app chooser
      Item {
        visible: kid.view === "apps"
        anchors.fill: parent
        anchors.margins: 32

        Text {
          id: chooserTitle
          text: "Apps in Kid Mode"
          color: "#ffffff"
          font.family: kid.touch.fontFamily
          font.pixelSize: 30
          font.weight: Font.Bold
        }
        Text {
          anchors.top: chooserTitle.bottom
          anchors.topMargin: 6
          text: kid.allowed.length + " chosen · tap to add or remove"
          color: Qt.rgba(1, 1, 1, 0.7)
          font.family: kid.touch.fontFamily
          font.pixelSize: 18
        }
        Button {
          anchors.right: parent.right
          anchors.top: parent.top
          width: 200; height: 60
          label: kid.enterAfterApps ? "Start" : "Done"
          fill: "#9be7a2"
          onActivated: {
            if (kid.enterAfterApps) kid.enter()
            else kid.view = kid.active ? "parent" : ""
          }
        }

        GridView {
          id: chooser
          anchors.top: chooserTitle.bottom
          anchors.topMargin: 48
          anchors.bottom: parent.bottom
          anchors.left: parent.left
          anchors.right: parent.right
          cellWidth: Math.floor(width / Math.max(3, Math.floor(width / 150)))
          cellHeight: 140
          clip: true
          boundsBehavior: Flickable.StopAtBounds
          model: kid.touch.apps
          delegate: Item {
            id: choice
            required property var modelData
            readonly property bool on: kid.allowed.indexOf(modelData.id) !== -1
            width: chooser.cellWidth
            height: chooser.cellHeight
            Rectangle {
              anchors.fill: parent
              anchors.margins: 6
              radius: 20
              color: choice.on ? "#9be7a2" : Qt.rgba(1, 1, 1, 0.08)
              Image {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 16
                width: 56; height: 56
                source: kid.touch.iconFor(choice.modelData, "")
                sourceSize.width: 112
                sourceSize.height: 112
                fillMode: Image.PreserveAspectFit
                asynchronous: true
              }
              Text {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 12
                horizontalAlignment: Text.AlignHCenter
                text: choice.modelData.name
                color: choice.on ? kid.ink : "#ffffff"
                font.family: kid.touch.fontFamily
                font.pixelSize: 15
                font.weight: choice.on ? Font.Bold : Font.Normal
                elide: Text.ElideRight
              }
              Text {
                visible: choice.on
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                text: String.fromCodePoint(0xF012C)
                color: kid.ink
                font.family: kid.touch.fontFamily
                font.pixelSize: 22
              }
              TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: kid.toggleAllowed(choice.modelData.id)
              }
            }
          }
        }
      }
    }
  }
}
