import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import Quickshell.Wayland
import qs.Commons
import qs.Commons as Commons

// Option A "Home & Dock" for the tablet port.
//
// - Home (app grid + open apps) shows whenever the current workspace is empty,
//   or on request from the dock / a swipe up from the handle.
// - The dock shows with Home. Apps run full screen; a slim handle replaces the
//   dock: tap it to peek at the dock, swipe up for Home.
// - Dock and handle step aside while the on-screen keyboard (wvkbd) is up.
// - Open apps lists every window: tap to switch, x to close.
//
// Loaded once (keepLoaded) as a `menu` plugin so it receives the shell's
// application library. IPC: omarchy-shell shell summon tablet.touch
// '{"view":"home"|"switcher"|"peek"}'; hide closes everything.
Item {
  id: root

  property string omarchyPath: Quickshell.env("OMARCHY_PATH")
  property var shell: null
  property var manifest: null

  // Third-party plugins cannot rely on the host's app-library facade, so read
  // desktop entries directly and launch them the way AppLibrary does.
  readonly property var apps: {
    var values = DesktopEntries.applications.values || []
    var out = []
    for (var i = 0; i < values.length; i++) {
      var e = values[i]
      if (!e || e.noDisplay || !e.name) continue
      if (root.configuredHides[e.id] === true || root.desktopHides[e.id] === true) continue
      out.push(e)
    }
    out.sort(function(a, b) {
      var x = a.name.toLowerCase(), y = b.name.toLowerCase()
      return x < y ? -1 : (x > y ? 1 : 0)
    })
    return out
  }
  readonly property string helper: "/opt/omarchy-tablet/ui/tablet-ui"

  // ------------------------------------------------------------------ state
  property bool homeRequested: false
  property bool switcherOpen: false
  property bool peeked: false
  property bool keyboardUp: false
  property string lastGesture: ""
  // After a screen size change (rotation) Quickshell surfaces keep a stale
  // horizontal scale until re-created: the handle was drawn squeezed to 1/1.6
  // in portrait and stretched 1.6x in landscape. Unmap every surface for a
  // moment when the size changes, then map them again at the new size.
  readonly property real screenW: Quickshell.screens.length > 0 ? Quickshell.screens[0].width : 0
  readonly property real screenH: Quickshell.screens.length > 0 ? Quickshell.screens[0].height : 0
  onScreenWChanged: remapAfterResize()
  onScreenHChanged: remapAfterResize()
  property bool remapping: false
  readonly property bool mapAllowed: !remapping
  function remapAfterResize() { remapping = true; remapTimer.restart() }
  Timer { id: remapTimer; interval: 250; onTriggered: root.remapping = false }
  // Latency probe: epoch ms of the first frame swapped after Open apps shows.
  property bool switcherFramePending: false
  property real switcherFrameAt: 0
  onSwitcherOpenChanged: if (switcherOpen) switcherFramePending = true
  // Latency probe: epoch ms when Home last changed state and its next frame.
  property real homeChangedAt: 0
  property real homeFrameAt: 0
  property bool homeFramePending: false
  onHomeVisibleChanged: { homeChangedAt = Date.now(); homeFramePending = true }

  readonly property bool opened: homeRequested || switcherOpen || peeked

  readonly property var workspaceWindows: {
    var ws = Hyprland.focusedWorkspace
    if (!ws || !ws.toplevels) return []
    return ws.toplevels.values
  }
  readonly property bool appOpen: workspaceWindows.length > 0
  readonly property bool kidActive: kids.active
  readonly property bool homeVisible: !kidActive && (homeRequested || !appOpen) && !switcherOpen
  readonly property bool dockVisible: !kidActive && !keyboardUp && (homeVisible || peeked || switcherOpen)
  // Stays up with the keyboard: closing a window can focus a terminal, which
  // raises the keys; the list must not vanish mid-task (cards sit at the top).
  readonly property bool switcherShown: !kidActive && switcherOpen
  readonly property bool handleVisible: !kidActive && appOpen && !homeVisible && !peeked && !switcherOpen

  // Every mapped window outside special workspaces, most recent first.
  readonly property var windows: {
    var values = Hyprland.toplevels.values
    var out = []
    for (var i = 0; i < values.length; i++) {
      var t = values[i]
      if (!t || !t.workspace || t.workspace.id < 0) continue
      out.push(t)
    }
    out.sort(function(a, b) { return root.focusRank(a) - root.focusRank(b) })
    return out
  }

  readonly property var terminalClasses: ["foot", "Alacritty", "com.mitchellh.ghostty", "kitty"]
  readonly property var browserClasses: ["chromium", "Chromium", "google-chrome", "brave-browser", "firefox"]

  // ---------------------------------------------------------- palette/type
  readonly property color bg: Commons.Color.background
  readonly property color fg: Commons.Color.foreground
  readonly property color accent: Commons.Color.accent
  readonly property color muted: Commons.Color.muted
  readonly property color surface: Qt.tint(bg, Qt.rgba(fg.r, fg.g, fg.b, 0.07))
  readonly property color surfaceHi: Qt.tint(bg, Qt.rgba(fg.r, fg.g, fg.b, 0.14))
  readonly property string fontFamily: Style.font.family

  readonly property int dockHeight: 84
  readonly property int handleHeight: 34

  function glyph(code) { return String.fromCodePoint(code) }

  // ------------------------------------------------------- plugin contract
  function open(payloadJson) {
    if (kidActive) return
    var payload = ({})
    try { payload = JSON.parse(payloadJson || "{}") } catch (e) { payload = ({}) }
    var view = String(payload.view || "home")
    if (view === "switcher") showSwitcher()
    else if (view === "peek") peek()
    else showHome()
  }

  function close() {
    homeRequested = false
    switcherOpen = false
    peeked = false
  }

  function ping() { return "ok" }

  function debug() {
    return JSON.stringify({
      shell: !!root.shell, apps: root.apps.length,
      grid: grid.count, gridHeight: grid.height, windows: root.windows.length,
      appOpen: root.appOpen, keyboardUp: root.keyboardUp,
      home: root.homeRequested, peeked: root.peeked, switcher: root.switcherOpen,
      lastGesture: root.lastGesture, switcherFrameAt: root.switcherFrameAt,
      homeChangedAt: root.homeChangedAt, homeFrameAt: root.homeFrameAt,
      geometry: {
        screen: handleWindow.screen ? [handleWindow.screen.width, handleWindow.screen.height] : null,
        handle: [handleWindow.width, handleWindow.height, handleArea.parent ? handleArea.parent.width : -1, handleArea.x],
        dock: [dockWindow.width, dockWindow.height], home: [homeWindow.width, homeWindow.height]
      },
      kid: { active: kids.active, view: kids.view, home: kids.homeVisible, strip: kids.stripVisible,
             allowed: kids.allowed, hasPin: kids.hasPin, classes: kids.config.classes }
    })
  }

  // ---------------------------------------------------------------- actions
  function showHome() {
    switcherOpen = false
    peeked = false
    homeRequested = true
  }

  function showSwitcher() {
    homeRequested = false
    peeked = false
    Hyprland.refreshToplevels()
    switcherOpen = true
  }

  function peek() {
    if (!appOpen || kidActive) return
    hideKeyboard()             // the handle sits under the keys while typing
    homeRequested = false
    switcherOpen = false
    peeked = true
    peekTimer.restart()
  }

  function run(args) { Quickshell.execDetached(args) }
  function hypr(expr) { run(["hyprctl", "-q", "dispatch", expr]) }

  function address(t) {
    var a = String(t && t.address || "")
    return a.indexOf("0x") === 0 ? a : "0x" + a
  }

  function windowClass(t) {
    if (!t) return ""
    if (t.wayland && t.wayland.appId) return String(t.wayland.appId)
    var ipc = t.lastIpcObject
    return ipc && ipc["class"] ? String(ipc["class"]) : ""
  }

  function focusRank(t) {
    var ipc = t ? t.lastIpcObject : null
    var rank = ipc && ipc.focusHistoryID !== undefined ? Number(ipc.focusHistoryID) : 999
    return t && t.activated ? -1 : rank
  }

  function focusWindow(t) {
    if (!t) return
    hypr('hl.dsp.focus({ window = "address:' + address(t) + '" })')
    close()
  }

  function closeWindow(t) {
    if (!t) return
    hypr('hl.dsp.window.close({ window = "address:' + address(t) + '" })')
  }

  function windowsOf(classes) {
    var out = []
    for (var i = 0; i < windows.length; i++)
      if (classes.indexOf(windowClass(windows[i])) !== -1) out.push(windows[i])
    return out
  }

  // Pinned apps switch to their most recent window, or start one.
  function openPinned(classes, action) {
    var open = windowsOf(classes)
    if (open.length > 0) focusWindow(open[0])
    else { run([helper, action]); close() }
  }

  function launchEntry(entry) {
    if (!entry || !entry.id) return
    run(["uwsm-app", "--", "gtk-launch", entry.id + ".desktop"])
    close()
  }

  function toggleKeyboard() { run([helper, "keyboard"]) }
  function hideKeyboard() { if (keyboardUp) run([helper, "keyboard-hide"]) }

  function windowName(t, entry) {
    if (entry && entry.name) return entry.name
    var cls = windowClass(t)
    return cls ? cls.charAt(0).toUpperCase() + cls.slice(1) : "Window"
  }

  function entryFor(t) {
    var cls = windowClass(t)
    return cls ? DesktopEntries.heuristicLookup(cls) : null
  }

  function iconFor(entry, fallback) {
    var name = String(entry && entry.icon ? entry.icon : (fallback || ""))
    if (name.charAt(0) === "/") return "file://" + name
    var themed = name ? Quickshell.iconPath(name, true) : ""
    return themed || Quickshell.iconPath("application-x-executable", true)
  }

  function pinnedIcon(classes, fallbackIcon) {
    var entry = DesktopEntries.heuristicLookup(classes[0])
    return iconFor(entry, fallbackIcon)
  }

  // Omarchy's own launcher hides (packaged list + desktop-file rules).
  property var configuredHides: ({})
  property var desktopHides: ({})

  function idSet(raw) {
    var out = ({})
    var lines = String(raw || "").split(/\n/)
    for (var i = 0; i < lines.length; i++) {
      var id = lines[i].trim()
      if (id.slice(-8) === ".desktop") id = id.slice(0, -8)
      if (id) out[id] = true
    }
    return out
  }

  FileView {
    path: root.omarchyPath + "/default/omarchy/launcher.hides"
    watchChanges: true
    onLoaded: root.configuredHides = root.idSet(text())
    onFileChanged: reload()
    onLoadFailed: root.configuredHides = ({})
  }

  Process {
    id: hiddenScan
    running: true
    command: ["bash", root.omarchyPath + "/shell/services/hidden-entries.sh",
              [Quickshell.env("XDG_CURRENT_DESKTOP"), Quickshell.env("XDG_SESSION_DESKTOP")].filter(function(v) { return !!v }).join(":")]
    stdout: StdioCollector { onStreamFinished: root.desktopHides = root.idSet(text) }
  }

  Connections {
    target: DesktopEntries
    function onApplicationsChanged() { hiddenScan.running = true }
  }

  // ------------------------------------------------------------ hyprland
  Connections {
    target: Hyprland
    function onRawEvent(event) {
      var name = event.name
      if (name === "openlayer" || name === "closelayer") {
        if (String(event.data).trim() === "wvkbd") root.keyboardUp = name === "openlayer"
      } else if (name === "openwindow" || name === "closewindow" || name === "movewindow"
                 || name === "activewindow" || name === "windowtitle") {
        refreshTimer.restart()
        if (name !== "windowtitle") kids.scheduleNormalize()
      } else if (name === "changefloatingmode" || name === "fullscreen") {
        kids.scheduleNormalize()
      }
      if (name === "openwindow") {
        if (root.kidActive) kids.windowOpened(event.data)
        else root.homeRequested = false
      }
    }
  }

  Timer {
    id: refreshTimer
    interval: 120
    onTriggered: Hyprland.refreshToplevels()
  }

  Timer {
    id: peekTimer
    interval: 6000
    onTriggered: root.peeked = false
  }

  // The keyboard may already be up when the shell (re)loads.
  Process {
    id: layerProbe
    command: ["hyprctl", "layers", "-j"]
    running: true
    stdout: StdioCollector {
      onStreamFinished: root.keyboardUp = text.indexOf('"wvkbd"') !== -1
    }
  }

  Component.onCompleted: Hyprland.refreshToplevels()

  // ======================================================== shared pieces
  component Glyph: Text {
    property int code: 0
    text: code ? String.fromCodePoint(code) : ""
    color: root.fg
    font.family: root.fontFamily
    font.pixelSize: 26
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
  }

  component Tap: Rectangle {
    id: tap
    signal activated()
    property bool current: false
    property color baseColor: "transparent"
    readonly property bool pressed: tapHandler.pressed
    radius: 14
    color: pressed ? root.surfaceHi : (current ? root.surface : baseColor)
    TapHandler {
      id: tapHandler
      gesturePolicy: TapHandler.ReleaseWithinBounds
      onTapped: tap.activated()
    }
  }

  component DockItem: Tap {
    id: item
    property string label: ""
    property int code: 0
    property string iconSource: ""
    property bool running: false
    width: 92
    height: 68
    Column {
      anchors.centerIn: parent
      spacing: 4
      Item {
        width: 32; height: 30
        anchors.horizontalCenter: parent.horizontalCenter
        Image {
          anchors.fill: parent
          visible: item.iconSource !== ""
          source: item.iconSource
          sourceSize.width: 64
          sourceSize.height: 64
          fillMode: Image.PreserveAspectFit
          asynchronous: true
        }
        Glyph {
          anchors.fill: parent
          visible: item.iconSource === ""
          code: item.code
        }
      }
      Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: item.label
        color: item.current ? root.fg : Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.78)
        font.family: root.fontFamily
        font.pixelSize: 13
      }
    }
    Rectangle {
      visible: item.running
      width: 6; height: 6; radius: 3
      color: root.accent
      anchors.horizontalCenter: parent.horizontalCenter
      anchors.bottom: parent.bottom
      anchors.bottomMargin: 3
    }
  }

  component DockSeparator: Rectangle {
    width: 1
    height: 40
    color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.14)
  }

  component Dock: Rectangle {
    id: dock
    width: row.implicitWidth + 24
    height: root.dockHeight - 12
    radius: 22
    color: root.surface
    border.width: 1
    border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.10)
    Row {
      id: row
      anchors.centerIn: parent
      spacing: 4
      DockItem {
        label: "Home"; code: 0xF02DC
        current: root.homeVisible
        onActivated: root.homeVisible && root.homeRequested ? root.close() : root.showHome()
      }
      DockItem {
        label: "Apps"; code: 0xF003B
        onActivated: { root.close(); root.run([root.helper, "apps"]) }
      }
      DockSeparator { anchors.verticalCenter: parent.verticalCenter }
      DockItem {
        label: "Terminal"; code: 0xF018D
        iconSource: root.pinnedIcon(root.terminalClasses, "utilities-terminal")
        running: root.windowsOf(root.terminalClasses).length > 0
        onActivated: root.openPinned(root.terminalClasses, "terminal")
      }
      DockItem {
        label: "Browser"; code: 0xF059F
        iconSource: root.pinnedIcon(root.browserClasses, "chromium")
        running: root.windowsOf(root.browserClasses).length > 0
        onActivated: root.openPinned(root.browserClasses, "browser")
      }
      DockSeparator { anchors.verticalCenter: parent.verticalCenter }
      DockItem {
        label: "Open apps"; code: 0xF0328
        current: root.switcherOpen
        running: root.windows.length > 0
        onActivated: root.switcherOpen ? root.close() : root.showSwitcher()
      }
      DockItem {
        label: "Keyboard"; code: 0xF030C
        onActivated: root.toggleKeyboard()
      }
    }
  }

  // Body and close button are separate, non-overlapping tap targets: a nested
  // TapHandler loses to its parent's on touch.
  component WindowCard: Rectangle {
    id: card
    property var toplevel: null
    property bool current: false
    readonly property var entry: root.entryFor(toplevel)
    signal activated()
    signal closeRequested()
    height: 76
    radius: 14
    color: body.pressed ? root.surfaceHi : root.surface
    Rectangle {
      visible: card.current
      width: 4; height: 36; radius: 2
      color: root.accent
      anchors.left: parent.left
      anchors.leftMargin: 6
      anchors.verticalCenter: parent.verticalCenter
    }
    Tap {
      id: body
      anchors.left: parent.left
      anchors.top: parent.top
      anchors.bottom: parent.bottom
      anchors.right: closeButton.left
      color: "transparent"
      onActivated: card.activated()
      Row {
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14
        Image {
          width: 40; height: 40
          anchors.verticalCenter: parent.verticalCenter
          source: root.iconFor(card.entry, root.windowClass(card.toplevel))
          sourceSize.width: 80
          sourceSize.height: 80
          fillMode: Image.PreserveAspectFit
          asynchronous: true
        }
        Column {
          anchors.verticalCenter: parent.verticalCenter
          width: parent.width - 54
          spacing: 2
          Text {
            width: parent.width
            text: root.windowName(card.toplevel, card.entry)
            color: root.fg
            font.family: root.fontFamily
            font.pixelSize: 17
            font.weight: Font.DemiBold
            elide: Text.ElideRight
          }
          Text {
            width: parent.width
            text: (card.toplevel ? card.toplevel.title : "") + (card.toplevel && card.toplevel.workspace ? "  ·  Space " + card.toplevel.workspace.name : "")
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.66)
            font.family: root.fontFamily
            font.pixelSize: 14
            elide: Text.ElideRight
          }
        }
      }
    }
    Tap {
      id: closeButton
      width: 72
      radius: 12
      anchors.right: parent.right
      anchors.top: parent.top
      anchors.bottom: parent.bottom
      anchors.margins: 6
      Glyph { anchors.centerIn: parent; code: 0xF0156; font.pixelSize: 22; color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.7) }
      onActivated: card.closeRequested()
    }
  }

  // ================================================================ Home
  PanelWindow {
    id: homeWindow
    screen: Quickshell.screens.length > 0 ? Quickshell.screens[0] : null
    visible: root.mapAllowed
    anchors { top: true; left: true; right: true; bottom: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Normal
    WlrLayershell.namespace: "tablet-home"
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

    readonly property bool portrait: height > width

    Item { id: homeProbe }
    Connections {
      target: homeProbe.Window.window
      function onFrameSwapped() {
        if (!root.homeFramePending) return
        root.homeFramePending = false
        root.homeFrameAt = Date.now()
      }
    }

    // Kept mapped: showing is a cheap repaint, not a new surface.
    mask: Region { width: root.homeVisible ? homeWindow.width : 0; height: root.homeVisible ? homeWindow.height : 0 }

    Item {
      anchors.fill: parent
      visible: root.homeVisible

      Rectangle {
        anchors.fill: parent
        color: root.bg
      }

      // Switch into Kid Mode (first use asks for a parent PIN and apps).
      Tap {
        z: 2
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 24
        width: kidLabel.implicitWidth + 64
        height: 56
        baseColor: root.surface
        onActivated: kids.start()
        Row {
          anchors.centerIn: parent
          spacing: 10
          Glyph { code: 0xF04CE; font.pixelSize: 22; color: "#ffd166"; anchors.verticalCenter: parent.verticalCenter }
          Text {
            id: kidLabel
            anchors.verticalCenter: parent.verticalCenter
            text: "Kid Mode"
            color: root.fg
            font.family: root.fontFamily
            font.pixelSize: 17
            font.weight: Font.DemiBold
          }
        }
      }

      Column {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 32
        anchors.topMargin: 26
        spacing: 18

        Row {
          spacing: 18
          Text {
            id: clockText
            text: Qt.formatTime(clock.date, "HH:mm")
            color: root.fg
            font.family: root.fontFamily
            font.pixelSize: 44
            font.weight: Font.Light
          }
          Text {
            anchors.baseline: clockText.baseline
            text: Qt.formatDate(clock.date, "dddd, MMMM d")
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.7)
            font.family: root.fontFamily
            font.pixelSize: 18
          }
        }

        // Continue where you left off: the three most recent windows.
        Flow {
          width: parent.width
          spacing: 10
          visible: root.windows.length > 0
          Repeater {
            model: root.windows.slice(0, homeWindow.portrait ? 2 : 3)
            delegate: Tap {
              id: chip
              required property var modelData
              readonly property var entry: root.entryFor(modelData)
              width: Math.min(320, (header.width - 20) / (homeWindow.portrait ? 2 : 3))
              height: 60
              baseColor: root.surface
              onActivated: root.focusWindow(modelData)
              Row {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 12
                Image {
                  width: 30; height: 30
                  anchors.verticalCenter: parent.verticalCenter
                  source: root.iconFor(chip.entry, root.windowClass(chip.modelData))
                  sourceSize.width: 60
                  sourceSize.height: 60
                  asynchronous: true
                }
                Text {
                  anchors.verticalCenter: parent.verticalCenter
                  width: parent.width - 42
                  text: chip.modelData.title || root.windowClass(chip.modelData)
                  color: root.fg
                  font.family: root.fontFamily
                  font.pixelSize: 15
                  elide: Text.ElideRight
                }
              }
            }
          }
        }
      }

      GridView {
        id: grid
        anchors.top: header.bottom
        anchors.topMargin: 18
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Math.max(12, Math.floor((homeWindow.width - columns * cellWidth) / 2))
        anchors.rightMargin: anchors.leftMargin
        // Home ends above the handle strip; the dock overlays the screen bottom.
        anchors.bottomMargin: root.keyboardUp ? 8 : root.dockHeight - root.handleHeight + 8
        readonly property int columns: Math.max(3, Math.floor((homeWindow.width - 40) / 128))
        cellWidth: 128
        cellHeight: 118
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.apps
        delegate: Item {
          id: cell
          required property var modelData
          width: grid.cellWidth
          height: grid.cellHeight
          Tap {
            anchors.fill: parent
            anchors.margins: 4
            onActivated: root.launchEntry(cell.modelData)
            Column {
              anchors.centerIn: parent
              width: parent.width - 8
              spacing: 8
              Image {
                width: 52; height: 52
                anchors.horizontalCenter: parent.horizontalCenter
                source: root.iconFor(cell.modelData, "")
                sourceSize.width: 104
                sourceSize.height: 104
                fillMode: Image.PreserveAspectFit
                asynchronous: true
              }
              Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: cell.modelData.name
                color: root.fg
                font.family: root.fontFamily
                font.pixelSize: 14
                maximumLineCount: 2
                wrapMode: Text.Wrap
                elide: Text.ElideRight
              }
            }
          }
        }
      }
    }
  }

  KidMode {
    id: kids
    touch: root
  }

  SystemClock {
    id: clock
    precision: SystemClock.Minutes
  }

  // ========================================================== Open apps
  PanelWindow {
    id: switcherWindow
    screen: Quickshell.screens.length > 0 ? Quickshell.screens[0] : null
    visible: root.mapAllowed
    anchors { top: true; left: true; right: true; bottom: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Normal
    WlrLayershell.namespace: "tablet-switcher"

    Item { id: switcherProbe }
    Connections {
      target: switcherProbe.Window.window
      function onFrameSwapped() {
        if (!root.switcherFramePending) return
        root.switcherFramePending = false
        root.switcherFrameAt = Date.now()
      }
    }
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

    // Kept mapped: showing is a cheap repaint, not a new surface.
    mask: Region { width: root.switcherShown ? switcherWindow.width : 0; height: root.switcherShown ? switcherWindow.height : 0 }

    Item {
      anchors.fill: parent
      visible: root.switcherShown

      Rectangle {
        anchors.fill: parent
        color: Qt.rgba(root.bg.r, root.bg.g, root.bg.b, 0.94)
        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: { root.lastGesture = "scrim"; root.close() } }
      }

      Column {
        anchors.top: parent.top
        anchors.topMargin: 28
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 48, 760)
        spacing: 12

        Item {
          width: parent.width
          height: 44
          Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "Open apps"
            color: root.fg
            font.family: root.fontFamily
            font.pixelSize: 24
            font.weight: Font.DemiBold
          }
          Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.windows.length === 1 ? "1 window" : root.windows.length + " windows"
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.6)
            font.family: root.fontFamily
            font.pixelSize: 15
          }
        }

        Text {
          visible: root.windows.length === 0
          text: "Nothing is open. Start an app from Home."
          color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.7)
          font.family: root.fontFamily
          font.pixelSize: 17
        }

        Flickable {
          width: parent.width
          height: Math.min(contentHeight, switcherWindow.height - 28 - 56 - root.dockHeight - 24)
          contentHeight: cards.implicitHeight
          clip: true
          boundsBehavior: Flickable.StopAtBounds
          Column {
            id: cards
            width: parent.width
            spacing: 10
            Repeater {
              model: root.windows
              delegate: WindowCard {
                required property var modelData
                width: cards.width
                toplevel: modelData
                current: modelData.activated
                onActivated: { root.lastGesture = "card"; root.focusWindow(modelData) }
                onCloseRequested: { root.lastGesture = "card-close"; root.closeWindow(modelData) }
              }
            }
          }
        }
      }
    }
  }

  // ================================================================ Dock
  PanelWindow {
    id: dockWindow
    screen: Quickshell.screens.length > 0 ? Quickshell.screens[0] : null
    visible: root.mapAllowed
    anchors { left: true; right: true; bottom: true }
    implicitHeight: root.dockHeight
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "tablet-dock"
    // Above Home/Open apps (Top). The keyboard shares Overlay but the dock
    // is never shown while it is up.
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

    // Kept mapped: showing is a cheap repaint, not a new surface.
    mask: Region { width: root.dockVisible ? dockWindow.width : 0; height: root.dockVisible ? dockWindow.height : 0 }

    Item {
      anchors.fill: parent
      visible: root.dockVisible

      Dock {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
      }
    }
  }

  // While peeking, a transparent catcher above the app dismisses the dock on
  // any tap away. Always mapped and never drawn; only its input mask changes.
  PanelWindow {
    id: peekCatcher
    screen: Quickshell.screens.length > 0 ? Quickshell.screens[0] : null
    visible: root.mapAllowed
    anchors { top: true; left: true; right: true; bottom: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "tablet-peek"
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    mask: Region { width: root.peeked ? peekCatcher.width : 0; height: root.peeked ? peekCatcher.height - root.dockHeight : 0 }
    Item {
      anchors.fill: parent
      anchors.bottomMargin: root.dockHeight
      TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: root.peeked = false }
    }
  }

  // ============================================================== Handle
  PanelWindow {
    id: handleWindow
    screen: Quickshell.screens.length > 0 ? Quickshell.screens[0] : null
    // Always mapped with a constant 34 px reservation (outside Kid Mode).
    // Mapping it only while an app showed made every Home, peek, Open apps
    // and keyboard change resize the app twice (a software re-render each).
    visible: !root.kidActive && root.mapAllowed
    anchors { left: true; right: true; bottom: true }
    implicitHeight: root.handleHeight
    color: root.bg
    exclusionMode: ExclusionMode.Auto
    WlrLayershell.namespace: "tablet-handle"
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    mask: Region { width: root.handleVisible ? handleWindow.width : 0; height: root.handleVisible ? handleWindow.height : 0 }

    Item {
      anchors.fill: parent
      visible: root.handleVisible

      Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.08)
      }

      // Tap: peek at the dock. Swipe up: Home.
      Item {
        id: handleArea
        width: 280
        height: parent.height
        anchors.horizontalCenter: parent.horizontalCenter
        Rectangle {
          anchors.centerIn: parent
          width: 120; height: 5; radius: 3
          color: handleTap.pressed || handleDrag.active ? root.fg : root.muted
        }
        // DragThreshold (passive until the finger moves) lets the swipe take over.
        TapHandler {
          id: handleTap
          gesturePolicy: TapHandler.DragThreshold
          onTapped: root.peek()
        }
        DragHandler {
          id: handleDrag
          target: null
          xAxis.enabled: false
          dragThreshold: 6
          // translation resets when the drag ends; keep the last offset.
          property real dy: 0
          onActiveTranslationChanged: dy = activeTranslation.y
          onActiveChanged: {
            if (active) { dy = 0; return }
            root.lastGesture = "handle-drag " + Math.round(dy)
            if (dy < -24) { root.hideKeyboard(); root.showHome() }
          }
        }
      }

      Tap {
        width: 64
        height: parent.height
        radius: 8
        anchors.right: parent.right
        anchors.rightMargin: 10
        Glyph { anchors.centerIn: parent; code: 0xF030C; font.pixelSize: 22; color: root.muted }
        onActivated: root.toggleKeyboard()
      }
    }
  }
}
