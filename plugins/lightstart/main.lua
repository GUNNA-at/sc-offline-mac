-- lightstart: sc-offline Lua plugin for machines short on memory.
-- A landing zone such as Orison needs more memory than a 24 GB Mac has, and the frame rate collapses while its data is
-- touched every frame. This script waits until you are in the world, then uses the mod's own commands to put you in
-- the pilot seat of a ship high over a moon: quantum.travel <place>, spawn.ship <class>, crew.sit pilot, crew.power_on.
-- It counts as done when the game reports that you are aboard a ship, and tries again if the spawn sequence pulls you
-- back. `lightstart.go` (mod menu, or a hotkey you bind) runs it by hand. Edit the constants to taste.

local PLACE      = "Daymar"               -- a name from data\locations.txt
local ALTITUDE   = 20000                  -- metres above the ground (the command allows 100..20000)
local SHIP       = "DRAK_Cutlass_Black"   -- a class from data\ships.txt
local SETTLE_MS  = 45000                  -- the camera must report a zone this long before the first attempt
local RETRY_MS   = 20000                  -- pause between attempts
local WATCH_MS   = 240000                 -- after success, keep checking this long that you are still aboard
local MAX_CYCLES = 8

local state, elapsed, stable, cycles, tries, watched = "boot", 0, 0, 0, 0, 0
local last_ms, argt = nil, nil

local function log(m) sco.log("info", m) end

local function arg_types()
  if argt then return argt end
  argt = {}
  local ok, list = pcall(sco.list_commands)
  if ok and type(list) == "table" then
    for _, c in ipairs(list) do
      local t = {}
      for i, a in ipairs(c.args or {}) do t[i] = a.type end
      argt[c.name] = t
    end
  end
  return argt
end

-- run a mod command, giving numbers the int or float type the command declares
local function run(name, ...)
  local t = arg_types()[name]
  if not t then log(name .. ": not registered"); return false end
  local a = { ... }
  for i, v in ipairs(a) do
    if type(v) == "number" then
      if t[i] == "int" then a[i] = math.tointeger(math.floor(v)) or 0
      elseif t[i] == "float" then a[i] = v + 0.0 end
    end
  end
  local ok, r1, r2 = sco.invoke(name, table.unpack(a))
  log(string.format("%s -> %s %s %s", name, tostring(ok), tostring(r1), tostring(r2 or "")))
  return ok
end

local function in_world()
  if not (sco.game and sco.game.world and sco.game.world.camera) then return false end
  local ok, cam = pcall(sco.game.world.camera)
  return ok and type(cam) == "table" and cam.zone ~= nil and cam.zone ~= 0
end

local function aboard()
  if not (sco.game and sco.game.vehicles and sco.game.vehicles.player_ship) then return false end
  local ok, ship = pcall(sco.game.vehicles.player_ship)
  return ok and ship ~= nil and ship ~= 0 and ship ~= false
end

local function begin(reason)
  if cycles >= MAX_CYCLES then log("giving up after " .. cycles .. " attempts"); state = "done"; return end
  cycles = cycles + 1; tries = 0; elapsed = 0; state = "travel"
  log("moving to " .. PLACE .. " (" .. reason .. "), attempt " .. cycles)
end

sco.register_command{
  name = "lightstart.go", title = "Light start: go now",
  help = "Put me in a ship at the light place now",
  fn = function() cycles = 0; begin("asked"); return "moving to " .. PLACE end,
}

sco.subscribe("game.ready", function()
  state = "watch"; stable = 0
  local t = arg_types()
  for _, n in ipairs{ "quantum.travel", "spawn.ship", "crew.sit", "crew.power_on" } do
    log(n .. (t[n] and (" (" .. table.concat(t[n], ", ") .. ")") or " MISSING"))
  end
end)

sco.subscribe("tick", function(_, ms)
  if state == "boot" or state == "done" then return end
  local dt = ms or 0                                   -- frame time, or a running clock
  if dt >= 10000 then local prev = last_ms; last_ms = dt; dt = prev and (dt - prev) or 0 end
  if dt < 0 or dt > 5000 then dt = 0 end
  elapsed = elapsed + dt

  if state == "watch" then
    if in_world() then stable = stable + dt else stable = 0 end
    if stable >= SETTLE_MS then begin("camera in a zone for " .. (SETTLE_MS // 1000) .. " s") end

  elseif state == "travel" and elapsed >= 1000 then
    elapsed = 0
    if aboard() then log("already aboard a ship"); state = "watching"; watched = 0
    elseif run("quantum.travel", PLACE, ALTITUDE) then state = "ship"
    else tries = tries + 1; if tries >= 5 then state = "pause" end end

  elseif state == "ship" and elapsed >= 2500 then
    run("spawn.ship", SHIP, 5); state = "seat"; elapsed = 0; tries = 0

  elseif state == "seat" and elapsed >= 2000 then
    elapsed = 0; tries = tries + 1
    if aboard() then run("crew.power_on"); log("aboard"); state = "watching"; watched = 0
    elseif tries > 15 then log("not seated after 30 s"); state = "pause"
    else run("crew.sit", "pilot") end

  elseif state == "pause" and elapsed >= RETRY_MS then
    begin("retry")

  elseif state == "watching" and elapsed >= 10000 then
    watched = watched + elapsed; elapsed = 0
    if not aboard() then begin("no longer aboard")
    elseif watched >= WATCH_MS then log("settled aboard after " .. cycles .. " attempt(s)"); state = "done" end
  end
end)

sco.status("Light start armed: " .. PLACE)
