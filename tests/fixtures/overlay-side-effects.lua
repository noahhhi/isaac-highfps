-- ISOLATED TEST ONLY: intentionally writes entity Position in render callbacks.
-- Purpose: detect duplicate/missing callbacks, U->R order changes, discarded
-- legitimate render mutations, and missing cached draw output at high refresh.
-- Deploy instead of the normal validation fixture; no separate input tool needed.
local mod = RegisterMod("HighFPS isolated overlay side effects", 1)
local game = Game()
local seed = "L4A6 X4X8"
local expected = Seeds.String2Seed(seed)
local pending, issued, active, done = false, false, false, false
local origin, startTime, sequence = 0, 0, 0
local u, pu, tu, r, pr, tr = 0, 0, 0, 0, 0, 0
local mainWrites, playerWrites, tearWrites = 0, 0, 0
local tear
local traced = {[10]=true,[11]=true,[30]=true,[31]=true,[59]=true,[60]=true,
                [90]=true,[91]=true,[120]=true,[121]=true,[149]=true,[150]=true}

local function frame() return game:GetFrameCount() - origin end
local function note(kind, force)
    sequence = sequence + 1
    if not active or (not force and not traced[frame()]) then return end
    local p = Isaac.GetPlayer(0)
    local tx, ty = -99999, -99999
    if tear and tear:Exists() then tx, ty = tear.Position.X, tear.Position.Y end
    Isaac.DebugString(string.format(
        "[HFOV] kind=%s seq=%d t=%.3f f=%d u=%d pu=%d tu=%d r=%d pr=%d tr=%d mw=%d pw=%d tw=%d px=%.6f py=%.6f tx=%.6f ty=%.6f",
        kind, sequence, (Isaac.GetTime()-startTime)/1000, frame(), u, pu, tu, r, pr, tr,
        mainWrites, playerWrites, tearWrites, p.Position.X, p.Position.Y, tx, ty))
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
    if not issued then pending, active = true, false; Isaac.DebugString("[HFV_SETUP] overlay seed restart"); return end
    assert(game:GetSeeds():GetStartSeed() == expected, "overlay fixture seed mismatch")
    assert(Isaac.GetPlayer(0):GetPlayerType() == PlayerType.PLAYER_ISAAC, "overlay fixture player mismatch")
    active, done, tear = true, false, nil
    origin, startTime, sequence = game:GetFrameCount(), Isaac.GetTime(), 0
    u, pu, tu, r, pr, tr, mainWrites, playerWrites, tearWrites = 0,0,0,0,0,0,0,0,0
    Isaac.DebugString("[HFTEST] started overlay-side-effects")
    Isaac.DebugString("[HFOV_META] seed="..expected.." stageSeed="..game:GetSeeds():GetStageSeed(game:GetLevel():GetStage()).." playerSeed="..Isaac.GetPlayer(0).InitSeed)
    note("START", true)
end)

mod:AddCallback(ModCallbacks.MC_INPUT_ACTION, function(_, entity, hook, action)
    if not active or not entity or not entity:ToPlayer() then return end
    if action >= ButtonAction.ACTION_LEFT and action <= ButtonAction.ACTION_SHOOTDOWN then
        if hook == InputHook.GET_ACTION_VALUE then return 0.0 end
        return false
    end
end)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
    if pending then
        pending, issued = false, true
        Isaac.GetPlayer(0):ChangePlayerType(PlayerType.PLAYER_ISAAC)
        Isaac.ExecuteCommand("seed "..seed)
        return
    end
    if not active or done then return end
    u = u + 1
    local p = Isaac.GetPlayer(0)
    -- Read before any fixture reset so that lost/multiplied render writes show.
    note("U", true)
    if frame() == 5 then
        p.Position, p.Velocity = Vector(320, 240), Vector(0, 0)
        note("RESET_PLAYER", true)
    end
    if frame() == 10 then
        tear = p:FireTear(Vector(160, 180), Vector(0, 0), false, false, false)
        tear.Height, tear.FallingSpeed, tear.FallingAcceleration = -20, 0, 0
        note("SPAWN_TEAR", true)
    end
    if tear and tear:Exists() then
        tear.Height, tear.FallingSpeed, tear.FallingAcceleration = -20, 0, 0
    end
    if frame() >= 300 then
        note("DONE", true)
        done = true
        Isaac.DebugString("[HFTEST] DONE overlay-side-effects")
    end
end)

mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, function(_, p)
    if active and not done and p.Index == Isaac.GetPlayer(0).Index then pu = pu + 1; note("PU", false) end
end)
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, function(_, t)
    if active and not done and tear and t.InitSeed == tear.InitSeed then tu = tu + 1; note("TU", false) end
end)

mod:AddCallback(ModCallbacks.MC_POST_RENDER, function()
    if not active or done then return end
    r = r + 1
    note("R_BEFORE", false)
    if frame() >= 5 then
        local p = Isaac.GetPlayer(0)
        p.Position = p.Position + Vector(0, 0.25)
        mainWrites = mainWrites + 1
    end
    Isaac.RenderText("MAIN "..r, 20, 20, 1, 1, 1, 1)
    Isaac.RenderText("M", 20 + (r % 60) * 2, 100, 1, 0.5, 0.5, 1)
    note("R_AFTER", false)
end)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_RENDER, function(_, p)
    if not active or done or p.Index ~= Isaac.GetPlayer(0).Index then return end
    pr = pr + 1
    note("PR_BEFORE", false)
    if frame() >= 5 then
        p.Position = p.Position + Vector(0.125, 0)
        playerWrites = playerWrites + 1
    end
    Isaac.RenderText("PLAYER "..pr, 20, 40, 0.5, 1, 0.5, 1)
    Isaac.RenderText("P", 20 + (pr % 60) * 2, 120, 0.5, 1, 0.5, 1)
    note("PR_AFTER", false)
end)
mod:AddCallback(ModCallbacks.MC_POST_TEAR_RENDER, function(_, t)
    if not active or done or not tear or t.InitSeed ~= tear.InitSeed then return end
    tr = tr + 1
    note("TR_BEFORE", false)
    t.Position = t.Position + Vector(0.125, 0)
    tearWrites = tearWrites + 1
    Isaac.RenderText("TEAR "..tr, 20, 60, 0.5, 0.5, 1, 1)
    Isaac.RenderText("T", 20 + (tr % 60) * 2, 140, 0.5, 0.5, 1, 1)
    note("TR_AFTER", false)
end)
