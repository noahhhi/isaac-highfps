-- ISOLATED COPY ONLY. This file controls the test player through Lua.
-- Parent runner must explicitly copy it into its isolated validation mod.
-- One case per process: set transition to nil, "WALK", "FADE", or "TELEPORT".
-- Fixture timestamps/counters are evidence, not a substitute for native frame traces.
local config = { transition = nil, duration = 180, seed = "L4A6 X4X8" }
local mod = RegisterMod("HighFPS isolated cadence census", 1)
local game = Game()
local active, done, requested = false, false, false
local origin, startTime, target = 0, 0, nil
local updates, renders, playerUpdates, playerRenders = 0, 0, 0, 0
local tearUpdates, tearRenders, newRooms = 0, 0, 0
local inputQueries, inputTriggerTrue = 0, 0
local tear
local pendingSeedRestart, seedRestartIssued = false, false
local expectedSeed = Seeds.String2Seed(config.seed)
assert(Seeds.IsStringValidSeed(config.seed) and expectedSeed ~= 0, "Invalid validation seed")

local function log(kind)
    if not active then return end
    local p = Isaac.GetPlayer(0)
    local x, y, vx, tx, ty = 0, 0, 0, -99999, -99999
    if p then x, y, vx = p.Position.X, p.Position.Y, p.Velocity.X end
    if tear and tear:Exists() then tx, ty = tear.Position.X, tear.Position.Y end
    Isaac.DebugString(string.format(
        "[HFV] kind=%s t=%.3f f=%d room=%d rf=%d paused=%d u=%d r=%d pu=%d pr=%d tu=%d tr=%d nr=%d iq=%d it=%d x=%.6f y=%.6f vx=%.6f tx=%.6f ty=%.6f",
        kind, (Isaac.GetTime() - startTime) / 1000, game:GetFrameCount() - origin,
        game:GetLevel():GetCurrentRoomIndex(), game:GetRoom():GetFrameCount(),
        game:IsPaused() and 1 or 0, updates, renders, playerUpdates, playerRenders,
        tearUpdates, tearRenders, newRooms, inputQueries, inputTriggerTrue, x, y, vx, tx, ty))
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
    -- Setting a seed after the floor exists does not regenerate that floor.
    -- Normalize once with a fresh Isaac restart, outside this start callback.
    if not seedRestartIssued then
        active, pendingSeedRestart = false, true
        Isaac.DebugString("[HFV_SETUP] pending_fixed_seed_restart=" .. config.seed)
        return
    end
    assert(game:GetSeeds():GetStartSeed() == expectedSeed,
        "Fixed seed restart failed; reject this case instead of testing a random layout")
    assert(Isaac.GetPlayer(0):GetPlayerType() == PlayerType.PLAYER_ISAAC,
        "Fixed seed restart did not select Isaac")
    active, done, requested = true, false, false
    origin, startTime, target = game:GetFrameCount(), Isaac.GetTime(), nil
    updates, renders, playerUpdates, playerRenders = 0, 0, 0, 0
    tearUpdates, tearRenders, newRooms, inputQueries, inputTriggerTrue = 0, 0, 0, 0, 0
    tear = nil
    Isaac.GetPlayer(0):ChangePlayerType(PlayerType.PLAYER_ISAAC)
    Isaac.DebugString("[HFTEST] started cadence=" .. (config.transition or "motion"))
    Isaac.DebugString("[HFV_META] seed=" .. game:GetSeeds():GetStartSeed() .. " stageSeed=" .. game:GetSeeds():GetStageSeed(game:GetLevel():GetStage()) .. " playerSeed=" .. Isaac.GetPlayer(0).InitSeed .. " case=" .. (config.transition or "motion"))
    log("start")
end)

mod:AddCallback(ModCallbacks.MC_INPUT_ACTION, function(_, entity, hook, action)
    if not active or not entity or not entity:ToPlayer() then return end
    if action < ButtonAction.ACTION_LEFT or action > ButtonAction.ACTION_DOWN then return end
    inputQueries = inputQueries + 1
    local f = game:GetFrameCount() - origin
    local pressed = not config.transition and not done and
        ((f >= 30 and f < 75 and action == ButtonAction.ACTION_RIGHT) or
         (f >= 100 and f < 145 and action == ButtonAction.ACTION_LEFT))
    if hook == InputHook.GET_ACTION_VALUE then return pressed and 1.0 or 0.0 end
    if hook == InputHook.IS_ACTION_PRESSED then return pressed end
    if hook == InputHook.IS_ACTION_TRIGGERED then
        local trigger = pressed and (f == 30 or f == 100)
        if trigger then inputTriggerTrue = inputTriggerTrue + 1 end
        return trigger
    end
end)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
    if pendingSeedRestart then
        pendingSeedRestart, seedRestartIssued = false, true
        -- SetStartSeed + restart is not reliable in J374: restart can select a
        -- fresh seed. The console seed command creates the actual seeded run.
        Isaac.GetPlayer(0):ChangePlayerType(PlayerType.PLAYER_ISAAC)
        Isaac.ExecuteCommand("seed " .. config.seed)
        return
    end
    if not active or done then return end
    updates = updates + 1
    local f, p = game:GetFrameCount() - origin, Isaac.GetPlayer(0)
    if config.transition then
        if f >= 30 and not requested then
            assert(RoomTransitionAnim[config.transition], "Unknown transition enum")
            for slot = 0, 7 do
                local door = game:GetRoom():GetDoor(slot)
                if door and door.TargetRoomIndex >= 0 then
                    local desc = game:GetLevel():GetRoomByIdx(door.TargetRoomIndex, -1)
                    if desc and desc.Data and desc.Data.Type == RoomType.ROOM_DEFAULT then
                        target, requested = door.TargetRoomIndex, true
                        p.Position = door.Position
                        log("transition_request_" .. config.transition)
                        game:StartRoomTransition(target, slot % 4, RoomTransitionAnim[config.transition], p, -1)
                        log("transition_call_return")
                        break
                    end
                end
            end
            assert(requested, "No normal adjacent room; case invalid")
        elseif requested and game:GetLevel():GetCurrentRoomIndex() == target and game:GetRoom():GetFrameCount() >= 10 then
            done = true
        end
    else
        if f == 5 then p.Position, p.Velocity = Vector(100, 280), Vector(0, 0) end
        if f == 80 then tear = p:FireTear(Vector(100, 180), Vector(2, 0), false, false, false) end
        if tear and tear:Exists() then
            tear.Height, tear.FallingSpeed, tear.FallingAcceleration = -20, 0, 0
        end
        if f >= config.duration then done = true end
    end
    log("update")
    if done then
        log("done")
        Isaac.DebugString("[HFTEST] DONE cadence=" .. (config.transition or "motion"))
    end
end)

mod:AddCallback(ModCallbacks.MC_POST_RENDER, function()
    if not active or done then return end
    renders = renders + 1
    if renders % 15 == 0 then log("render_mark") end
end)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE, function(_, p)
    if active and not done and p.Index == Isaac.GetPlayer(0).Index then playerUpdates = playerUpdates + 1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_RENDER, function(_, p)
    if active and not done and p.Index == Isaac.GetPlayer(0).Index then playerRenders = playerRenders + 1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE, function(_, t)
    if active and not done and tear and t.InitSeed == tear.InitSeed then tearUpdates = tearUpdates + 1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_TEAR_RENDER, function(_, t)
    if active and not done and tear and t.InitSeed == tear.InitSeed then tearRenders = tearRenders + 1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
    if active and not done then newRooms = newRooms + 1; log("new_room") end
end)
