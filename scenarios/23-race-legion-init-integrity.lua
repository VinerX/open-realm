-- Init-integrity scenario for 23-Race Legion.
--
-- The map registers fifteen deferred init steps on OnInit._run() and collects
-- any failure in the globals InitErrors/InitFatal.  A boot that merely reaches
-- game start can still have most of that queue failed; this scenario asserts
-- the custom initialization actually completed and left behind the triggers and
-- tables the rest of the map depends on.
--
-- It observes through the same globals the map itself inspects, so it exercises
-- exactly the native surface InitCustomTriggers/RunInitializationTriggers use.
--
-- Returns nil while advancing, "PASS" to finish, or a failure string.

local SETTLE_FRAMES = 200

-- Triggers RunInitializationTriggers executes with ConditionalTriggerExecute.
local REQUIRED_TRIGGERS = {
  'gg_trg_Unit_Indexer', 'gg_trg_InitForEconomics', 'gg_trg_UnitUpgraded',
  'gg_trg_MainInfo', 'gg_trg_Initial_things', 'gg_trg_Init',
  'gg_trg_InitGlobals', 'gg_trg_Owner', 'gg_trg_PortalFix',
}

-- Globals the deferred steps populate and later systems read.
local REQUIRED_GLOBALS = {
  'udg_AllPlayers', 'udg_IncomeTimerFirst', 'udg_Flagmans',
  'udg_ZahvatBuildings', 'udg_B_InKalim',
}

function scenario_step(frame)
  if frame < SETTLE_FRAMES then
    return nil
  end

  if type(InitErrors) ~= 'table' then
    return 'FAIL: InitErrors is ' .. type(InitErrors) .. ', expected table'
  end
  if InitFatal ~= nil or #InitErrors > 0 then
    -- Surface every failed step so the audit log names the exact natives and
    -- steps still missing.  InitFatal is only the first failure, so dump the
    -- whole InitErrors table rather than reporting it alone.
    for i = 1, #InitErrors do
      print('WC3_INIT_ERROR ' .. tostring(InitErrors[i]))
    end
    return 'FAIL: ' .. tostring(#InitErrors) .. ' init step(s) failed; WC3_INIT_ERROR lines list them'
  end

  for _, name in ipairs(REQUIRED_TRIGGERS) do
    local value = _G[name]
    if value == nil then
      return 'FAIL: ' .. name .. ' was never created'
    end
    if not IsTriggerEnabled(value) and IsTriggerEnabled(value) == nil then
      return 'FAIL: ' .. name .. ' is not a trigger'
    end
  end

  for _, name in ipairs(REQUIRED_GLOBALS) do
    if _G[name] == nil then
      return 'FAIL: global ' .. name .. ' was never initialized'
    end
  end

  return 'PASS'
end
