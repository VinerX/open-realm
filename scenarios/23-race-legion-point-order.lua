-- Verify Lua point-order dispatch and issued-order state on the loaded map.

local unit

function scenario_step(frame)
  if frame == 201 then
    local template = gg_unit_n03B_0658
    if template == nil or GetUnitState(template, 0) <= 0 then
      return 'FAIL: expected living map-authored unit gg_unit_n03B_0658'
    end

    unit = CreateUnit(GetOwningPlayer(template), GetUnitTypeId(template),
      GetUnitX(template), GetUnitY(template), 0.0)
    if unit == nil then
      return 'FAIL: CreateUnit returned nil for point-order fixture'
    end

    if not IssuePointOrder(unit, 'move', GetUnitX(unit) + 256.0, GetUnitY(unit)) then
      RemoveUnit(unit)
      unit = nil
      return 'FAIL: IssuePointOrder(move) was rejected'
    end
    if GetUnitCurrentOrder(unit) ~= OrderId('move') then
      RemoveUnit(unit)
      unit = nil
      return 'FAIL: accepted point order did not become current order'
    end

    RemoveUnit(unit)
    unit = nil
    return 'PASS'
  end

  if frame > 201 then
    if unit ~= nil then RemoveUnit(unit) end
    return 'FAIL: point-order scenario missed its target frame'
  end
end
