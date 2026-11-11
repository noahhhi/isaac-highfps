-- ISOLATED OWNED TEST MOD ONLY. This fixture writes only this Mod's SaveData.
-- Before an independent run use a new caseToken or remove only this test mod's
-- isolated saveX.dat. Reusing a terminal token fails closed; it never restarts.
-- Phase is persisted BEFORE seed/room/use calls, surviving Lua VM replacement.
local mod=RegisterMod("HighFPS isolated rewind lifecycle",1)
local json=require("json")
local game=Game()
local config={seed="L4A6 X4X8",caseToken="rewind-v1-set-unique-case-token"}
local expected=Seeds.String2Seed(config.seed)
local item=CollectibleType.COLLECTIBLE_GLOWING_HOUR_GLASS
local state=nil
local active=false
local terminalLogged=false
local function load()
    if not mod:HasData() then return nil end
    local ok,value=pcall(json.decode,mod:LoadData())
    if ok and type(value)=="table" and value.schema==1 and value.caseToken==config.caseToken then return value end
    return nil
end
local function save() mod:SaveData(json.encode(state)) end
local function note(kind)
    if not state then return end
    Isaac.DebugString(string.format("[HFRW] kind=%s token=%s phase=%s t=%.3f gf=%d ui=%d tc=%d room=%d rf=%d stage=%d paused=%d u=%d r=%d pu=%d phaseUpdates=%d used=%d resumes=%d",
        kind,config.caseToken,state.phase,(Isaac.GetTime()-(state.startTime or Isaac.GetTime()))/1000,
        game:GetFrameCount(),Isaac.GetFrameCount(),game.TimeCounter,game:GetLevel():GetCurrentRoomIndex(),
        game:GetRoom():GetFrameCount(),game:GetLevel():GetStage(),game:IsPaused() and 1 or 0,
        state.u or 0,state.r or 0,state.pu or 0,state.phaseUpdates or 0,state.used and 1 or 0,state.resumes or 0))
end
local function fail(reason)
    state=state or {schema=1,caseToken=config.caseToken}
    state.phase,state.reason="failed",reason
    save();active=false
    Isaac.DebugString("[HFRW_ERROR] token="..config.caseToken.." reason="..reason)
    error(reason)
end
local function phase(value)
    state.phase,state.phaseUpdates=value,0
    save()
end
local function resume(source)
    state=load()
    if not state then return false end
    if state.phase=="done" or state.phase=="failed" then
        if not terminalLogged then
            terminalLogged=true
            Isaac.DebugString("[HFRW_ERROR] terminal_case_token_reused="..config.caseToken.." phase="..state.phase)
        end
        return false
    end
    if state.phase=="seed_pending" or state.phase=="seed_requested" then return false end
    if game:GetSeeds():GetStartSeed()~=expected then return false end
    active=true
    state.resumes=(state.resumes or 0)+1
    save()
    Isaac.DebugString("[HFTEST] started rewind-resume source="..source)
    note("vm_resume")
    return true
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    -- Reload from the correct active save slot, never trust menu-slot data.
    state=load()
    if not state then
        state={schema=1,caseToken=config.caseToken,phase="seed_pending",used=false,
               u=0,r=0,pu=0,phaseUpdates=0,resumes=0}
        save();active=false
        return
    end
    if state.phase=="seed_requested" then
        if game:GetSeeds():GetStartSeed()~=expected then fail("fixed_seed_restart_failed");return end
        state.startTime=Isaac.GetTime()
        state.seed=expected
        state.originalRoom=game:GetLevel():GetCurrentRoomIndex()
        state.originalStage=game:GetLevel():GetStage()
        state.u,state.r,state.pu=0,0,0
        state.used=false
        local p=Isaac.GetPlayer(0)
        assert(p:GetPlayerType()==PlayerType.PLAYER_ISAAC,"rewind fixture player mismatch")
        p:AddCollectible(item,0,false)
        phase("warmup")
        active=true
        Isaac.DebugString("[HFTEST] started rewind")
        Isaac.DebugString("[HFRW_META] token="..config.caseToken.." seed="..expected.." stageSeed="..game:GetSeeds():GetStageSeed(state.originalStage).." playerSeed="..p.InitSeed.." useFlags="..UseFlag.USE_OWNED)
        note("start")
        return
    end
    resume("game_started")
end)
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,entity,hook,action)
    if not active or not entity or not entity:ToPlayer() then return end
    if action>=ButtonAction.ACTION_LEFT and action<=ButtonAction.ACTION_SHOOTDOWN then
        if hook==InputHook.GET_ACTION_VALUE then return 0.0 end
        return false
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM,function()
    if not active then resume("new_room") end
    if active then note("new_room") end
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if not state then state=load() end
    if state and state.phase=="seed_pending" then
        phase("seed_requested") -- durable before a restart can replace this VM
        Isaac.GetPlayer(0):ChangePlayerType(PlayerType.PLAYER_ISAAC)
        Isaac.ExecuteCommand("seed "..config.seed)
        return
    end
    if not active and not resume("update") then return end
    if not state or state.phase=="done" or state.phase=="failed" then return end
    state.u=state.u+1
    state.phaseUpdates=state.phaseUpdates+1
    note("update")
    local p,room=Isaac.GetPlayer(0),game:GetRoom()
    local roomIndex=game:GetLevel():GetCurrentRoomIndex()
    if state.phase=="warmup" and state.phaseUpdates>=30 then
        local target,direction,position=nil,nil,nil
        for slot=0,7 do
            local door=room:GetDoor(slot)
            if door and door.TargetRoomIndex>=0 then
                local desc=game:GetLevel():GetRoomByIdx(door.TargetRoomIndex,-1)
                if desc and desc.Data and desc.Data.Type==RoomType.ROOM_DEFAULT then
                    target,direction,position=door.TargetRoomIndex,slot%4,door.Position;break
                end
            end
        end
        if not target then fail("no_normal_adjacent_room");return end
        state.targetRoom,state.direction=target,direction
        phase("walking")
        p.Position=position
        note("walk_request")
        game:StartRoomTransition(target,direction,RoomTransitionAnim.WALK,p,-1)
        note("walk_return")
    elseif state.phase=="walking" and roomIndex==state.targetRoom and room:GetFrameCount()>=10 then
        phase("in_target");note("target_ready")
    elseif state.phase=="in_target" and state.phaseUpdates>=5 then
        if state.used then fail("duplicate_hourglass_use_blocked");return end
        state.used=true
        state.useRequestGameFrame=game:GetFrameCount()
        state.useRequestUiFrame=Isaac.GetFrameCount()
        phase("rewind_requested") -- durable one-shot latch BEFORE UseActiveItem
        note("use_request")
        -- Match normal owned use without USE_NOANIM/NOCOSTUME: preserve the
        -- visual/lifecycle path. CharEnh itself uses the flags overload.
        p:UseActiveItem(item,UseFlag.USE_OWNED,ActiveSlot.SLOT_PRIMARY,0)
        note("use_return") -- no SaveData here: a replacement VM may have advanced it
    elseif state.phase=="rewind_requested" and roomIndex==state.originalRoom then
        if game:GetLevel():GetStage()~=state.originalStage then fail("rewind_returned_to_wrong_stage");return end
        phase("returned");note("returned")
    elseif state.phase=="returned" and roomIndex==state.originalRoom and state.phaseUpdates>=10 and not game:IsPaused() then
        phase("done");note("done");active=false
        Isaac.DebugString("[HFTEST] DONE rewind")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE,function(_,p)
    if active and state and p.Index==Isaac.GetPlayer(0).Index then state.pu=state.pu+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if not active or not state then return end
    state.r=state.r+1
    if state.r%6==0 then note("render") end
end)
