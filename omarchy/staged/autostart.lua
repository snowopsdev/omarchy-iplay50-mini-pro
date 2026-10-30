-- Omarchy's packaged autostart starts its shell and normal desktop services.
hl.on("hyprland.start", function()
  hl.exec_cmd("uwsm finalize")
  hl.exec_cmd("/opt/omarchy-tablet/ui/tablet-ui keyboard-start")
  hl.exec_cmd("/opt/omarchy-tablet/tablet-orientation watch")
  hl.exec_cmd("uwsm-app -t service -- foot")
end)
