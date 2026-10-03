local picker, target_x, target_y

function scenario_step(frame)
  if frame < 200 then return nil end
  if not picker then
    if #InitErrors > 0 then return 'FAIL: map initialization failed' end
    picker = CreateUnit(Player(0), FourCC('h0HJ'), -3864, 398, 0)
    for x = -5000, 5000, 500 do
      for y = -5000, 5000, 500 do
        local p = Location(x, y)
        local valid = not IsTerrainPathableBJ(p, PATHING_TYPE_WALKABILITY)
          and not RectContainsLoc(gg_rct_HostRegion, p)
          and not RectContainsLoc(gg_rct_TestRegion, p)
          and not RectContainsLoc(gg_rct_EmeraldDream, p)
          and not RectContainsLoc(gg_rct_KillDalaran, p)
          and not RectContainsLoc(gg_rct_TurtleIsland, p)
          and not RectContainsLoc(gg_rct_Naxramas, p)
        RemoveLocation(p)
        if valid then target_x, target_y = x, y; break end
      end
      if target_x then break end
    end
    if not target_x then return 'FAIL: no valid mouse destination' end
    scenario_mouse(0, 3, target_x, target_y)
    return nil
  end
  if #InitErrors > 0 then
    for _, err in ipairs(InitErrors) do print('WC3_MOUSE_ERROR ' .. err) end
    return 'FAIL: mouse callback errors'
  end
  if math.abs(GetUnitX(picker) - target_x) > 1 or math.abs(GetUnitY(picker) - target_y) > 1 then
    return 'FAIL: right mouse event did not teleport race selector'
  end
  RemoveUnit(picker)
  return 'PASS'
end
