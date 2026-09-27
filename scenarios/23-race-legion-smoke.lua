-- Deterministic scenario for 23-Race Legion.
--
-- Runs after the map's config()/main().  It advances a bounded number of
-- frames so the authored triggers and timers get a chance to run, then
-- asserts observable world state through the same natives the map uses.
--
-- Returns nil while advancing, "PASS" to finish, or a failure string.

local SETTLE_FRAMES = 200
local PLAYER_SLOT_STATE_PLAYING = 1

function scenario_step(frame)
  if frame < SETTLE_FRAMES then
    return nil
  end

  -- config() declared a 24-slot map; that must survive into gameplay setup.
  if GetBJMaxPlayers() < 24 then
    return 'FAIL: GetBJMaxPlayers()=' .. tostring(GetBJMaxPlayers()) .. ' expected >=24'
  end

  -- The map must own a neutral-passive player slot for its critters.
  if Player(GetPlayerNeutralPassive()) == nil then
    return 'FAIL: neutral passive player is nil'
  end

  local playing = 0
  for i = 0, 23 do
    local p = Player(i)
    if p ~= nil and GetPlayerSlotState(p) == PLAYER_SLOT_STATE_PLAYING then
      playing = playing + 1
    end
  end
  if playing == 0 then
    return 'FAIL: no playing player slots after ' .. SETTLE_FRAMES .. ' frames'
  end

  return 'PASS'
end

