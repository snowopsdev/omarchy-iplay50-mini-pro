-- Keep the panel's native mode; rotate the logical output and touch together.
hl.env("GDK_SCALE", "1")
local transform = 0
local state = io.open(os.getenv("HOME") .. "/.local/state/omarchy/tablet-transform", "r")
if state then
  local saved = tonumber(state:read("*l"))
  state:close()
  if saved and saved >= 0 and saved <= 3 then transform = math.floor(saved) end
end
hl.monitor({ output = "DSI-1", mode = "1200x1920@60", position = "0x0", scale = 1.5,
             transform = transform })
hl.config({ input = { touchdevice = { output = "DSI-1", transform = transform } } })
