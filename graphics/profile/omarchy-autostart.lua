-- User module override for Omarchy 4.0.6's default.hypr.autostart.
-- The packaged startup exports every compositor variable to application
-- services. Reset graphics variables for each startup command, so the GPU
-- compositor and applications have separate environments (app-env picks the
-- GPU or software profile). Graphics variables are cleared from the user
-- manager first, so one the profile unsets does not linger from UWSM.
-- Review against /usr/share/omarchy/default/hypr/autostart.lua on updates.
local function app(command)
  local quoted = "'" .. command:gsub("'", "'\\''") .. "'"
  hl.exec_cmd("/opt/omarchy-tablet/gpu/app-env sh -c " .. quoted)
end

hl.on("hyprland.start", function()
  app("systemctl --user unset-environment GALLIUM_DRIVER LIBGL_ALWAYS_SOFTWARE MESA_LOADER_DRIVER_OVERRIDE VK_DRIVER_FILES PANVK_KBASE_DRI3 && systemctl --user import-environment $(env | cut -d'=' -f 1) && dbus-update-activation-environment --systemd --all && omarchy-launch-shell")
  app("omarchy-provision-first-run")
  app("omarchy-powerprofiles-init")
  app(o.launch("omarchy-hyprland-monitor-watch"))
  app(o.launch("udiskie --automount --no-notify --no-tray"))
  app("sleep 2 && omarchy-hook post-boot")
end)
