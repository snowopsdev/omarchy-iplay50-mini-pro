#!/usr/bin/python
"""Build a root-owned runtime overlay from the installed Omarchy package."""
from pathlib import Path
import os
import shutil
import re

source = Path('/usr/share/omarchy')
runtime = Path('/opt/omarchy-tablet/omarchy-runtime')
stage = runtime.with_name('omarchy-runtime.next')
previous = runtime.with_name('omarchy-runtime.previous')
if stage.exists():
    shutil.rmtree(stage)
stage.mkdir()
for entry in source.iterdir():
    if entry.name == 'shell':
        shutil.copytree(entry, stage / entry.name)
    else:
        (stage / entry.name).symlink_to(entry)

menu = stage / 'shell/plugins/menu/Menu.qml'
text = menu.read_text()
old = '    anchors { top: true; bottom: true; left: true; right: true }\n    color: "transparent"\n    WlrLayershell.namespace: "omarchy-menu"'
new = '''    // Tablet compatibility: leave the keyboard outside the menu's scrim.
    anchors { top: true; left: true; right: true }
    implicitHeight: Math.max(200, (screen ? screen.height : 1280) - (screen && screen.width > screen.height ? 260 : 360))
    color: "transparent"
    WlrLayershell.namespace: "omarchy-menu"'''
if old not in text:
    raise SystemExit('Omarchy menu changed; review the tablet compatibility patch')
text = text.replace(old, new, 1)
text = text.replace('  function open(payloadJson) {', '''  Process {
    id: tabletKeyboard
    command: ["/opt/omarchy-tablet/ui/tablet-ui", "keyboard-show"]
  }

  function open(payloadJson) {
    tabletKeyboard.running = true''', 1)
menu.write_text(text)
text = text.replace('WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive',
                    'WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand', 1)
text = text.replace('    onVisibleChanged: if (!visible) { cardTop = -1; maxRowsHeight = -1 }',
'''    onVisibleChanged: {
      if (!visible) { cardTop = -1; maxRowsHeight = -1 }
      else Qt.callLater(function() { keyCatcher.forceActiveFocus() })
    }''', 1)
old = '''          height: root.headerHeight
          radius: root.cornerRadius
          color: "transparent"'''
new = old + '''

          TapHandler {
            acceptedDevices: PointerDevice.TouchScreen
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: keyCatcher.forceActiveFocus()
          }'''
if text.count(old) != 1:
    raise SystemExit('Omarchy search header changed; review tablet focus patch')
menu.write_text(text.replace(old, new, 1))

# Use a real touch grab rather than Qt's synthesized mouse click. The bar's
# mouse reorder area can otherwise cancel a finger tap after a tiny movement.
bar = stage / 'shell/plugins/bar/Bar.qml'
text = bar.read_text()
old = '''    MouseArea {
      id: modulePointer'''
new = '''    TapHandler {
      acceptedDevices: PointerDevice.TouchScreen
      gesturePolicy: TapHandler.ReleaseWithinBounds
      onTapped: function(eventPoint, button) {
        root.pressModuleClickTarget(slot, Qt.LeftButton,
                                    eventPoint.position.x, eventPoint.position.y)
      }
    }

    MouseArea {
      id: modulePointer'''
if text.count(old) != 1:
    raise SystemExit('Omarchy bar changed; review the tablet touch patch')
bar.write_text(text.replace(old, new, 1))

# A transparent wallpaper surface forces unnecessary software alpha blending.
background = stage / 'shell/plugins/background/Background.qml'
content = background.read_text()
old = '      color: "transparent"'
if content.count(old) != 1:
    raise SystemExit('Omarchy background changed; review the opacity patch')
background.write_text(content.replace(old, '      color: Commons.Color.background', 1))
content = background.read_text()
old = '      visible: !remapGuard.remapping'
if content.count(old) != 1:
    raise SystemExit('Omarchy background visibility changed; review plain mode')
background.write_text(content.replace(old, old + ' && Quickshell.env("TABLET_PLAIN_BACKGROUND") !== "1"', 1))

# Remove finite chrome transitions; never change looping activity animations.
for relative in ['Ui/KeyboardPanel.qml', 'Ui/PopupCard.qml',
                 'Ui/WidgetButton.qml', 'plugins/bar/Bar.qml']:
    path = stage / 'shell' / relative
    content = path.read_text()
    if 'loops:' in content:
        raise SystemExit(f'Review animation loops before adapting {relative}')
    path.write_text(re.sub(r'\bduration:\s*\d+', 'duration: 0', content))
if previous.exists():
    shutil.rmtree(previous)
if runtime.exists():
    runtime.rename(previous)
stage.rename(runtime)
Path('/etc/omarchy.conf').write_text('OMARCHY_PATH=/opt/omarchy-tablet/omarchy-runtime\n')
print('Prepared Omarchy runtime with tablet menu geometry and direct bar touch')
