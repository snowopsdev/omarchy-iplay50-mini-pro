-- Minimal software-rendered test for the tablet's downstream DRM driver.
hl.monitor({ output = "DSI-1", mode = "1200x1920@60", position = "0x0", scale = 1.5 })
hl.config({
  animations = { enabled = false },
  decoration = { rounding = 0, blur = { enabled = false }, shadow = { enabled = false } },
  general = { gaps_in = 0, gaps_out = 0, border_size = 1 },
  misc = { disable_hyprland_logo = true, disable_splash_rendering = true },
  render = { direct_scanout = 0, use_fp16 = 0, cm_enabled = false },
  cursor = { no_hardware_cursors = true },
  debug = { vfr = true, enable_stdout_logs = true },
  ecosystem = { no_update_news = true, no_donation_nag = true },
})
hl.on("hyprland.start", function() hl.exec_cmd("foot") end)
