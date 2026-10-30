import QtQuick
import qs.Ui

WidgetButton {
  id: root
  property string moduleName
  property var settings
  text: settings ? settings.text : ""
  foreground: bar ? bar.foreground : "white"
  fontSize: 16
  horizontalMargin: 10
  onPressed: if (root.bar && root.settings.onClick) root.bar.run(root.settings.onClick)
}
