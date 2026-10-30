-- Keep the hardware-proven settings after Omarchy's defaults and theme load.
-- (Final state on the tablet, 2026-10-29.)
hl.config({
  animations = { enabled = true },
  decoration = { blur = { enabled = false }, shadow = { enabled = false } },
  render = { direct_scanout = 0, use_fp16 = 0, cm_enabled = false,
             new_render_scheduling = true },
  cursor = { no_hardware_cursors = true },
  misc = { disable_hyprland_logo = true, disable_splash_rendering = true,
           background_color = 0xff1a1b26 },
  -- Track changed regions; the software-frame completion shim protects them.
  debug = { vfr = true, damage_tracking = 2 },
  input = { repeat_delay = 800, repeat_rate = 25 },
})

-- Opaque windows avoid repeatedly blending the full desktop on the CPU.
o.window(".*", { opacity = "1.0 override 1.0 override" })

-- A delayed touch release must never turn a single screen tap into repeats.
hl.device({ name = "hl-virtual-keyboard-wvkbd-mobintl", repeat_rate = 0 })

-- The keyboard must remain above full-screen menu scrims to receive touch.
hl.layer_rule({ match = { namespace = "wvkbd" }, order = -100, no_anim = true })

-- Option A, 2026-10-29: shell/layer fades only. Window animations (open,
-- close, move/resize, window fades, workspace slide) made the compositor
-- redraw Chromium's software-rendered buffers every frame, so they stay off.
-- (They were first suspected of Chromium artifacts; the real causes were
-- tiling resizes and the gl-finish shortcut, see docs/09. Could be retried.)
hl.animation({ leaf = "windows", enabled = false })
hl.animation({ leaf = "windowsIn", enabled = false })
hl.animation({ leaf = "windowsOut", enabled = false })
hl.animation({ leaf = "windowsMove", enabled = false })
hl.animation({ leaf = "fadeIn", enabled = false })
hl.animation({ leaf = "fadeOut", enabled = false })
hl.animation({ leaf = "workspaces", enabled = false })
hl.animation({ leaf = "layersIn", enabled = true, speed = 2.5, bezier = "easeOutQuint", style = "fade" })

-- Option A: one app at a time (switch with Open apps). The monocle layout
-- gives every tiled window the whole workspace, so opening an app never
-- resizes the one on screen; side-by-side tiling made Chromium (software-
-- rendered, ~0.5 s to repaint) show cropped, striped stale frames.
-- Floating windows (dialogs, calculator, pickers) stay floating.
hl.config({ general = { layout = "monocle" } })

-- Performance QA, 2026-10-29: apps are full screen one at a time, so gaps
-- and borders only waste space, and Hyprland's first size guess for a new
-- window ignores the border, costing an extra resize (a full software
-- re-render) before the window could appear.
hl.config({ general = { gaps_in = 0, gaps_out = 0, border_size = 0 } })
