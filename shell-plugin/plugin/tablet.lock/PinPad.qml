import QtQuick
import qs.Commons
import qs.Commons as Commons

// PIN dots, a message line and a 3x4 keypad, shared by the lock screen and
// the PIN setup sheet. The owner keeps the entered digits; this only draws
// them and reports key presses.
Column {
  id: pad

  property int entered: 0          // digits typed so far
  property int length: 0           // known PIN length (0: grow with input)
  property string message: ""
  property bool error: false
  property bool busy: false        // a check is running: keys do nothing
  property bool locked: false      // waiting out a lockout: keys do nothing
  property string leftLabel: ""    // bottom-left key ("" hides it)
  property string okLabel: ""      // a wide button under the keys ("" hides it)
  property bool okEnabled: true
  property color fg: Commons.Color.foreground
  property color accent: Commons.Color.accent
  readonly property bool keysEnabled: !busy && !locked

  signal digit(string d)
  signal erase()
  signal okPressed()
  signal leftPressed()

  function shake() { shakeAnim.restart() }

  spacing: 22

  readonly property int key: 82
  readonly property int gap: 22

  Item {
    width: keys.width
    height: 22
    Row {
      id: dots
      anchors.centerIn: parent
      spacing: 18
      Repeater {
        model: Math.max(pad.length > 0 ? pad.length : 4, pad.entered)
        Rectangle {
          required property int index
          width: 16; height: 16; radius: 8
          color: index < pad.entered ? (pad.error ? Commons.Color.urgent : pad.fg) : "transparent"
          border.width: 2
          border.color: pad.error ? Commons.Color.urgent : Qt.rgba(pad.fg.r, pad.fg.g, pad.fg.b, 0.7)
        }
      }
      SequentialAnimation on anchors.horizontalCenterOffset {
        id: shakeAnim
        running: false
        loops: 1
        NumberAnimation { to: -18; duration: 45 }
        NumberAnimation { to: 16; duration: 70 }
        NumberAnimation { to: -12; duration: 65 }
        NumberAnimation { to: 8; duration: 60 }
        NumberAnimation { to: 0; duration: 50 }
      }
    }
  }

  Text {
    width: keys.width
    height: 24
    horizontalAlignment: Text.AlignHCenter
    text: pad.busy ? "Checking…" : pad.message
    color: pad.error ? Qt.lighter(Commons.Color.urgent, 1.4) : Qt.rgba(pad.fg.r, pad.fg.g, pad.fg.b, 0.8)
    font.family: Style.font.family
    font.pixelSize: 17
    elide: Text.ElideRight
  }

  Grid {
    id: keys
    columns: 3
    spacing: pad.gap
    opacity: pad.keysEnabled ? 1 : 0.45

    Repeater {
      model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "L", "0", "R"]
      Rectangle {
        id: k
        required property string modelData
        readonly property bool isDigit: modelData !== "L" && modelData !== "R"
        readonly property bool shown: isDigit || (modelData === "L" ? pad.leftLabel !== "" : pad.entered > 0)
        width: pad.key; height: pad.key; radius: pad.key / 2
        opacity: shown ? 1 : 0
        color: isDigit
          ? (tap.pressed ? Qt.rgba(pad.fg.r, pad.fg.g, pad.fg.b, 0.32) : Qt.rgba(pad.fg.r, pad.fg.g, pad.fg.b, 0.12))
          : (tap.pressed ? Qt.rgba(pad.fg.r, pad.fg.g, pad.fg.b, 0.18) : "transparent")
        Text {
          anchors.centerIn: parent
          text: k.isDigit ? k.modelData
            : (k.modelData === "L" ? pad.leftLabel : String.fromCodePoint(0xF006E))
          color: pad.fg
          font.family: Style.font.family
          font.pixelSize: k.isDigit ? 34 : (k.modelData === "L" ? 18 : 30)
          font.weight: k.isDigit ? Font.Light : Font.DemiBold
        }
        TapHandler {
          id: tap
          enabled: k.shown && (pad.keysEnabled || k.modelData === "L")
          gesturePolicy: TapHandler.ReleaseWithinBounds
          onTapped: {
            if (k.isDigit) pad.digit(k.modelData)
            else if (k.modelData === "L") pad.leftPressed()
            else pad.erase()
          }
        }
      }
    }
  }
  Rectangle {
    visible: pad.okLabel !== ""
    width: keys.width
    height: 60
    radius: 30
    color: okTap.pressed ? Qt.rgba(pad.fg.r, pad.fg.g, pad.fg.b, 0.32) : Qt.rgba(pad.fg.r, pad.fg.g, pad.fg.b, 0.16)
    opacity: pad.okEnabled && pad.keysEnabled ? 1 : 0.4
    Text {
      anchors.centerIn: parent
      text: pad.okLabel
      color: pad.fg
      font.family: Style.font.family
      font.pixelSize: 19
      font.weight: Font.DemiBold
    }
    TapHandler {
      id: okTap
      enabled: pad.okEnabled && pad.keysEnabled
      gesturePolicy: TapHandler.ReleaseWithinBounds
      onTapped: pad.okPressed()
    }
  }
}
