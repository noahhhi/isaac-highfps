-- ISOLATED FIXTURE. Natural trapdoor entry avoids the historically unreliable
-- Lua StartStageTransition argument bridge. Never uses the stage console command.
local mod=RegisterMod("HighFPS isolated floor transition",1)
local game=Game()
local seed="L4A6 X4X8"
local pending,issued,active,done,requested,newLevel=false,false,false,false,false,false
local origin,startTime,u,r,pu,afterLevel,initialStage=0,0,0,0,0,0,0
local lastPaused=nil
local trapIndex=nil
local trapPosition=nil
local function note(kind)
    local level=game:GetLevel()
    local trap=trapIndex and not newLevel and game:GetRoom():GetGridEntity(trapIndex) or nil
    local trapType,trapState,trapFrame,trapAnim=-1,-1,-1,"none"
    if trap then
        trapType,trapState=trap:GetType(),trap.State
        trapFrame,trapAnim=trap:GetSprite():GetFrame(),trap:GetSprite():GetAnimation():gsub("%s","_")
    end
    Isaac.DebugString(string.format("[HFFLOOR] kind=%s t=%.3f gf=%d f=%d ui=%d tc=%d room=%d rf=%d stage=%d stageType=%d paused=%d u=%d r=%d pu=%d afterLevel=%d trapType=%d trapState=%d trapFrame=%d trapAnim=%s",
        kind,(Isaac.GetTime()-startTime)/1000,game:GetFrameCount(),game:GetFrameCount()-origin,
        Isaac.GetFrameCount(),game.TimeCounter,level:GetCurrentRoomIndex(),game:GetRoom():GetFrameCount(),
        level:GetStage(),level:GetStageType(),game:IsPaused() and 1 or 0,u,r,pu,afterLevel,trapType,trapState,trapFrame,trapAnim))
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    if not issued then pending,active=true,false;return end
    assert(game:GetSeeds():GetStartSeed()==Seeds.String2Seed(seed),"floor fixture seed mismatch")
    active,done,requested,newLevel=true,false,false,false
    origin,startTime=game:GetFrameCount(),Isaac.GetTime()
    u,r,pu,afterLevel=0,0,0,0
    initialStage=game:GetLevel():GetStage()
    trapIndex,trapPosition=nil,nil
    lastPaused=game:IsPaused()
    Isaac.DebugString("[HFTEST] started floor-transition")
    Isaac.DebugString("[HFFLOOR_META] seed="..game:GetSeeds():GetStartSeed().." stageSeed="..game:GetSeeds():GetStageSeed(initialStage).." playerSeed="..Isaac.GetPlayer(0).InitSeed.." path=natural_trapdoor")
    note("start")
end)
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,entity,hook,action)
    if not active or not entity or not entity:ToPlayer() then return end
    if action>=ButtonAction.ACTION_LEFT and action<=ButtonAction.ACTION_SHOOTDOWN then
        if hook==InputHook.GET_ACTION_VALUE then return 0.0 end
        return false
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_NEW_LEVEL,function()
    if not active or done or not requested then return end
    newLevel,afterLevel=true,0
    note("new_level")
end)
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM,function()
    if active and not done and requested then note("new_room") end
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if pending then
        pending,issued=false,true
        Isaac.GetPlayer(0):ChangePlayerType(PlayerType.PLAYER_ISAAC)
        Isaac.ExecuteCommand("seed "..seed)
        return
    end
    if not active or done then return end
    u=u+1
    if newLevel then afterLevel=afterLevel+1 end
    note("update")
    if not trapIndex and game:GetFrameCount()-origin>=5 then
        local room,p=game:GetRoom(),Isaac.GetPlayer(0)
        trapIndex=room:GetGridIndex(room:GetCenterPos())
        trapPosition=room:GetGridPosition(trapIndex)
        p.Position,p.Velocity=trapPosition+Vector(0,80),Vector(0,0)
        note("trap_spawn_request")
        local trap=Isaac.GridSpawn(GridEntityType.GRID_TRAPDOOR,0,trapPosition,true)
        assert(trap and trap:GetType()==GridEntityType.GRID_TRAPDOOR,"natural trapdoor spawn failed")
        note("trap_spawn_return")
    end
    if not requested and game:GetFrameCount()-origin>=30 then
        requested=true
        note("request")
        local p=Isaac.GetPlayer(0)
        p.Position,p.Velocity=trapPosition,Vector(0,0)
        note("entered_trapdoor")
    elseif newLevel and afterLevel>=10 and not game:IsPaused() then
        assert(game:GetLevel():GetStage()~=initialStage,"new-level callback did not advance stage")
        note("done")
        done=true
        Isaac.DebugString("[HFTEST] DONE floor-transition")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE,function(_,p)
    if active and not done and p.Index==Isaac.GetPlayer(0).Index then pu=pu+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if not active or done then return end
    r=r+1
    local paused=game:IsPaused()
    if paused~=lastPaused then lastPaused=paused;note(paused and "paused" or "unpaused") end
    if r%6==0 then note("render") end
end)
