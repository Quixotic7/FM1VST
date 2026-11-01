-- a4data: FM1VST's test droplet (core-glue/ripple): data.load at init (the console says what it found), a key saves
-- {count = n + 1} with data.save, and plays a Faust voice.
local d, synth

function init()
  d = data.load() or {count = 0}
  print("a4data count " .. d.count)
  synth = current.load("voice")
  flow.set(synth)
end

function key(k, vel)
  synth:note(util.note(k), vel / 127)
  if vel > 0 then
    d.count = d.count + 1
    data.save(d)
    print("a4data saved " .. d.count)
  end
end

function redraw()
  screen.clear()
  screen.text(20, 100, "a4data: " .. d.count)
  screen.update()
end
