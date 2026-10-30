import QtQuick
import qs.Ui

// Native Omarchy click registration and the runtime's direct touch dispatcher.
WidgetButton {
  id: root
  property string moduleName
  property var settings
  text: settings ? settings.text : ""
  foreground: bar ? bar.foreground : "white"
  fontSize: 18
  horizontalMargin: 14
  fixedWidth: text.length <= 2 ? 56 : (text === "Desktop" ? 112 : 80)
  tooltipText: settings && settings.description ? settings.description : ""
  onPressed: if (root.bar && root.settings.onClick) root.bar.run(root.settings.onClick)
}
