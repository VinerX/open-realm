local sent = false

function scenario_step(frame)
  if frame < 200 then return nil end
  if not sent then
    if #InitErrors > 0 then return 'FAIL: map initialization failed' end
    local group = CreateGroup()
    GroupEnumUnitsOfPlayer(group, Player(0), nil)
    for i = 0, BlzGroupGetSize(group) - 1 do
      local info = scenario_unit_commands(BlzGroupUnitAt(group, i))
      print('WC3_COMMAND_CARD type=' .. info.type .. ' abilities=' .. tostring(info.abilities) .. ' commands=' .. table.concat(info, ',') .. ' sell=' .. tostring(info.sell_units) .. ' trains=' .. tostring(info.trains))
    end
    DestroyGroup(group)
    local picker = CreateUnit(Player(0), FourCC('h0HJ'), -3864, 398, 0)
    local info = scenario_unit_commands(picker)
    print('WC3_RACE_CARD type=' .. info.type .. ' abilities=' .. tostring(info.abilities) .. ' commands=' .. table.concat(info, ',') .. ' sell=' .. tostring(info.sell_units) .. ' trains=' .. tostring(info.trains))
    RemoveUnit(picker)
    scenario_chat(0, '-ai1')
    sent = true
    return nil
  end
  if #InitErrors > 0 then
    for _, err in ipairs(InitErrors) do print('WC3_CHAT_ERROR ' .. err) end
    return 'FAIL: chat callback errors: ' .. #InitErrors
  end
  if udg_AiControl[0] ~= true then return 'FAIL: -ai1 did not activate player 0 AI' end
  return 'PASS'
end
