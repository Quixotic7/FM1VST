-- a3loop: FM1VST's test droplet (core-glue/ripple): SEL or a key starts a loop that never ends, first inside the
-- droplet's own pcall; the count hook must stop it anyway.
function init() print("a3loop up") end
local function forever()
  local n = 0
  pcall(function() while true do n = n + 1 end end)
  while true do n = n + 1 end
end
function key(k, vel)
  if vel > 0 then forever() end
end
function button(name, z)
  if name == "SEL" and z == 1 then forever() end
end
function redraw()
  screen.clear()
  screen.text(20, 100, "a3loop: press a key")
  screen.update()
end
