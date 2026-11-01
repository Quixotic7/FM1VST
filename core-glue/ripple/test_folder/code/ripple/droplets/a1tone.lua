-- a1tone: FM1VST's test droplet (core-glue/ripple). The keys play Plaits' virtual analog (a C++ current compiled in)
-- through the delay; a word from /lib/a1lib.lua on the screen and the console.
local lib = require("a1lib")
local synth, delay

function init()
  synth = current.load("plaits-va")
  delay = current.load("delay")
  flow.set(synth, delay)
  print("a1tone " .. lib.word())
end

function key(k, vel)
  synth:note(util.note(k), vel / 127)
end

function redraw()
  screen.clear()
  screen.level(15)
  screen.text(20, 100, "a1tone: " .. lib.word())
  screen.update()
end
