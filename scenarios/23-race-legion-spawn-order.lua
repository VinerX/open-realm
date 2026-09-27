-- Verify map-script unit creation, ownership, and an immediate order.

local unit

local function fail(detail)
  if unit ~= nil then
    RemoveUnit(unit)
    unit = nil
  end
  return 'FAIL: ' .. detail
end

function scenario_step(frame)
  if frame < 201 then
    return nil
  end

  if frame == 201 then
    local templates = {
      { name = 'gg_unit_h08O_0444', unit = gg_unit_h08O_0444 },
      { name = 'gg_unit_h0E2_0011', unit = gg_unit_h0E2_0011 },
      { name = 'gg_unit_h089_0405', unit = gg_unit_h089_0405 },
      { name = 'gg_unit_h08A_0406', unit = gg_unit_h08A_0406 },
    }
    local template, template_name
    local observed = {}
    for _, candidate in ipairs(templates) do
      local candidate_unit = candidate.unit
      if candidate_unit ~= nil then
        local life = GetUnitState(candidate_unit, 0)
        local max_life = GetUnitState(candidate_unit, 1)
        observed[#observed + 1] = candidate.name .. '=' .. tostring(life) ..
          '/' .. tostring(max_life)
        if life > 0 and template == nil then
          template = candidate_unit
          template_name = candidate.name
        end
      else
        observed[#observed + 1] = candidate.name .. '=nil'
      end
    end
    if template == nil then
      return 'FAIL: no living map-authored template: ' .. table.concat(observed, ', ')
    end
    local unit_id = GetUnitTypeId(template)
    local owner = GetOwningPlayer(template)
    local template_life = GetUnitState(template, 0)
    unit = CreateUnit(owner, unit_id, GetUnitX(template) + 128.0,
      GetUnitY(template), 0.0)
    if unit == nil then
      return 'FAIL: CreateUnit returned nil for map-authored unit'
    end
    if GetUnitTypeId(unit) ~= unit_id then
      return fail('spawned unit type does not match its map-authored template')
    end
    if GetOwningPlayer(unit) ~= owner then
      return fail('spawned unit owner does not match its map-authored template')
    end
    if not IssueImmediateOrder(unit, 'stop') then
      return fail('IssueImmediateOrder(stop) was rejected; life=' ..
        tostring(GetUnitState(unit, 0)) .. ', template life=' .. tostring(template_life) ..
        ', template=' .. template_name .. ', unit=' .. tostring(unit_id) ..
        ', order=' .. tostring(GetUnitCurrentOrder(unit)))
    end
    if GetUnitCurrentOrder(unit) ~= OrderId('stop') then
      return fail('unit current order does not match stop')
    end
    RemoveUnit(unit)
    unit = nil
    return 'PASS'
  end

  if frame > 201 then
    return 'FAIL: spawn/order scenario did not run on its target frame'
  end
end
