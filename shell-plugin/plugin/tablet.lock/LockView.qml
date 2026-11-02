import QtQuick
import Quickshell
import Quickshell.Services.UPower
import qs.Commons
import qs.Commons as Commons

// What the session lock surface shows. "cover": clock, date, battery and a
// swipe-up hint. "pin": the keypad. All state lives in the service (`lock`);
// this only draws it and forwards touches.
Item {
  id: view

  required property var lock

  readonly property color fg: "#f2f2f2"
  readonly property bool portrait: height > width
  readonly property bool onPin: lock.view === "pin"
  readonly property var battery: UPower.displayDevice

  SystemClock { id: clock; precision: SystemClock.Minutes }

  // The service hands over a pre-blurred, 480 px copy of the wallpaper.
  Rectangle { anchors.fill: parent; color: Commons.Color.background }
  Image {
    anchors.fill: parent
    source: view.lock.backgroundUrl
    fillMode: Image.PreserveAspectCrop
    smooth: true
    asynchronous: true
    cache: false
  }
  Rectangle {
    anchors.fill: parent
    color: "black"
    opacity: view.onPin ? 0.62 : 0.38
    Behavior on opacity { NumberAnimation { duration: 180 } }
  }

  // Any touch counts as activity (keeps the screen on).
  MouseArea {
    anchors.fill: parent
    property real startY: 0
    onPressed: function(m) { startY = m.y; view.lock.poke() }
    onReleased: function(m) {
      // Cover: swipe up or tap opens the keypad. Keypad: tap outside the keys
      // does nothing; a swipe down goes back to the cover.
      if (!view.onPin) view.lock.showPin()
      else if (m.y - startY > 120) view.lock.showCover()
    }
  }

  // ---------------------------------------------------------------- cover
  Column {
    id: coverClock
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.top: parent.top
    anchors.topMargin: view.onPin ? 36 : parent.height * (view.portrait ? 0.2 : 0.16)
    spacing: 4
    Behavior on anchors.topMargin { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    Text {
      anchors.horizontalCenter: parent.horizontalCenter
      text: Qt.formatTime(clock.date, "HH:mm")
      color: view.fg
      font.family: Style.font.family
      font.pixelSize: view.onPin ? 48 : 128
      font.weight: Font.Light
      Behavior on font.pixelSize { NumberAnimation { duration: 180 } }
    }
    Text {
      anchors.horizontalCenter: parent.horizontalCenter
      visible: !view.onPin
      text: Qt.formatDate(clock.date, "dddd, MMMM d")
      color: Qt.rgba(1, 1, 1, 0.85)
      font.family: Style.font.family
      font.pixelSize: 24
    }
  }

  Row {
    anchors.top: parent.top
    anchors.right: parent.right
    anchors.margins: 22
    spacing: 8
    visible: view.battery && view.battery.ready && view.battery.isLaptopBattery
    Text {
      anchors.verticalCenter: parent.verticalCenter
      text: String.fromCodePoint(view.battery && view.battery.state === UPowerDeviceState.Charging ? 0xF0084 : 0xF0079)
      color: view.fg
      font.family: Style.font.family
      font.pixelSize: 20
    }
    Text {
      anchors.verticalCenter: parent.verticalCenter
      text: view.battery ? Math.round(view.battery.percentage * 100) + "%" : ""
      color: view.fg
      font.family: Style.font.family
      font.pixelSize: 17
    }
  }

  Column {
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 40
    spacing: 6
    visible: !view.onPin
    Text {
      anchors.horizontalCenter: parent.horizontalCenter
      text: String.fromCodePoint(0xF0143)
      color: view.fg
      font.family: Style.font.family
      font.pixelSize: 30
    }
    Text {
      anchors.horizontalCenter: parent.horizontalCenter
      text: view.lock.pinSet ? "Swipe up to unlock" : "No lock PIN is set. Tap to unlock"
      color: Qt.rgba(1, 1, 1, 0.85)
      font.family: Style.font.family
      font.pixelSize: 18
    }
  }

  // ----------------------------------------------------------------- pin
  Column {
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: view.portrait ? 90 : 36
    spacing: 16
    visible: view.onPin

    Text {
      anchors.horizontalCenter: parent.horizontalCenter
      text: "Enter PIN"
      color: view.fg
      font.family: Style.font.family
      font.pixelSize: 22
      font.weight: Font.DemiBold
    }

    PinPad {
      id: pad
      fg: view.fg
      entered: view.lock.entered.length
      length: view.lock.pinLength
      message: view.lock.message
      error: view.lock.messageIsError
      busy: view.lock.checking
      locked: view.lock.waiting
      leftLabel: "Cancel"
      onDigit: function(d) { view.lock.pressDigit(d) }
      onErase: view.lock.eraseDigit()
      onLeftPressed: view.lock.showCover()
    }
  }

  Connections {
    target: view.lock
    function onWrongPin() { pad.shake() }
  }
}
