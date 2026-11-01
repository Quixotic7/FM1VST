-- a2err: FM1VST's test droplet (core-glue/ripple): SEL or a key calls a function that is not there (a runtime error).
function init() print("a2err up") end
function key(k, vel)
  if vel > 0 then not_a_function(k) end
end
function button(name, z)
  if name == "SEL" and z == 1 then not_a_function(name) end
end
function redraw()
  screen.clear()
  screen.text(20, 100, "a2err: press SEL or a key")
  screen.update()
end
