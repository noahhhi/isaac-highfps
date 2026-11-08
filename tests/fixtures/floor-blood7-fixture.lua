-- ISOLATED TEST ONLY. Observe ALL type1000/variant7 Blood Splat callbacks.
-- Native Update sets flag8 (RenderFloor); log before/after initialization and
-- the actual Entity.FrameCount, not the cache-age value in native diagnostics.
local mod=RegisterMod("HighFPS isolated floor blood callbacks",1)
local game=Game()
local seed="L4A6 X4X8"
local active,pending,issued,done=false,false,false,false
local origin,startTime,seq,u,r=0,0,0,0,0,0
local counts={init=0,update=0,render=0,remove=0}
local instances={}
local birthSource="engine"
local npc=nil
local function frame()return game:GetFrameCount()-origin end
local function identity(e)return tostring(GetPtrHash(e))..":"..tostring(e.InitSeed) end
local function log(kind,e,extra)
    seq=seq+1
    local parts={"kind="..kind,"seq="..seq,"t="..string.format("%.6f",(Isaac.GetTime()-startTime)/1000),
        "f="..frame(),"gf="..game:GetFrameCount(),"ui="..Isaac.GetFrameCount(),"u="..u,"r="..r,
        "init="..counts.init,"update="..counts.update,"render="..counts.render,"remove="..counts.remove}
    if e then
        local key=identity(e)
        local state=instances[key]
        parts[#parts+1]="entity="..GetPtrHash(e)
        parts[#parts+1]="seed="..e.InitSeed
        parts[#parts+1]="ef="..e.FrameCount
        parts[#parts+1]="floor="..(e:HasEntityFlags(8) and 1 or 0)
        parts[#parts+1]=string.format("x=%.6f y=%.6f",e.Position.X,e.Position.Y)
        parts[#parts+1]="source="..(state and state.source or "unobserved_init")
        parts[#parts+1]="bornGF="..(state and state.bornGF or -1)
        parts[#parts+1]="bornUI="..(state and state.bornUI or -1)
        parts[#parts+1]="ownUpdate="..(state and state.updates or 0)
        parts[#parts+1]="ownRender="..(state and state.renders or 0)
    end
    if extra then parts[#parts+1]=extra end
    Isaac.DebugString("[HFB7] "..table.concat(parts," "))
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    if not issued then pending,active=true,false;return end
    assert(game:GetSeeds():GetStartSeed()==Seeds.String2Seed(seed),"blood7 seed mismatch")
    active,done=true,false
    origin,startTime,seq,u,r=game:GetFrameCount(),Isaac.GetTime(),0,0,0
    counts={init=0,update=0,render=0,remove=0};instances={};npc=nil;birthSource="engine"
    Isaac.DebugString("[HFTEST] started blood7")
    Isaac.DebugString("[HFB7_META] seed="..game:GetSeeds():GetStartSeed().." stageSeed="..game:GetSeeds():GetStageSeed(game:GetLevel():GetStage()).." playerSeed="..Isaac.GetPlayer(0).InitSeed.." floorFlag=8 all_variant7=1")
    log("start")
end)
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,e,hook,action)
    if not active or not e or not e:ToPlayer() then return end
    if action>=ButtonAction.ACTION_LEFT and action<=ButtonAction.ACTION_SHOOTDOWN then
        if hook==InputHook.GET_ACTION_VALUE then return 0.0 end
        return false
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT,function(_,e)
    if not active or done then return end
    counts.init=counts.init+1
    instances[identity(e)]={source=birthSource,bornGF=game:GetFrameCount(),bornUI=Isaac.GetFrameCount(),updates=0,renders=0}
    log("effect_init",e)
end,7)
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_UPDATE,function(_,e)
    if not active or done then return end
    counts.update=counts.update+1
    local state=instances[identity(e)]
    if state then state.updates=state.updates+1 end
    log("effect_update",e)
end,7)
mod:AddCallback(ModCallbacks.MC_POST_EFFECT_RENDER,function(_,e)
    if not active or done then return end
    counts.render=counts.render+1
    local state=instances[identity(e)]
    if state then state.renders=state.renders+1 end
    log("effect_render",e)
    -- A deliberate Lua side effect and visible marker for EVERY variant7.
    -- No Position/Velocity or native flags are written by this callback.
    e:GetData().HFB7RenderWrites=(e:GetData().HFB7RenderWrites or 0)+1
    Isaac.RenderText("BLOOD7 CALLBACK "..counts.render,360,250,1,.25,.25,1)
end,7)
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE,function(_,e)
    if not active or done or e.Type~=EntityType.ENTITY_EFFECT or e.Variant~=7 then return end
    counts.remove=counts.remove+1
    log("effect_remove",e)
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
    local f=frame()
    if f==5 then
        npc=Isaac.Spawn(EntityType.ENTITY_GAPER,0,0,Vector(160,230),Vector(0,0),nil):ToNPC()
        npc:AddEntityFlags(EntityFlag.FLAG_FREEZE)
        npc.EntityCollisionClass=EntityCollisionClass.ENTCOLL_NONE
        log("npc_spawn",nil,"hp="..npc.HitPoints)
    end
    if f>=30 and f<=300 and f%30==0 then
        if npc and npc:Exists() and not npc:IsDead() then
            birthSource="npc_damage"
            local before=npc.HitPoints
            local accepted=npc:TakeDamage(.25,0,EntityRef(Isaac.GetPlayer(0)),0)
            log("npc_damage",nil,"accepted="..tostring(accepted).." hpBefore="..before.." hpAfter="..npc.HitPoints)
            birthSource="engine"
        else
            log("npc_missing")
        end
        birthSource="direct_spawn"
        log("direct_spawn_request")
        local effect=Isaac.Spawn(EntityType.ENTITY_EFFECT,7,0,Vector(320,210),Vector(0,0),Isaac.GetPlayer(0)):ToEffect()
        assert(effect and effect.Variant==7,"direct blood7 spawn failed")
        log("direct_spawn_return",effect)
        birthSource="engine"
    end
    log("world_update")
    if f>=330 then
        for key,state in pairs(instances) do
            log("instance_summary",nil,"identity="..key.." source="..state.source.." bornGF="..state.bornGF.." ownUpdate="..state.updates.." ownRender="..state.renders)
        end
        log("done");done=true
        Isaac.DebugString("[HFTEST] DONE blood7")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if not active or done then return end
    r=r+1
    if r%6==0 then log("main_render") end
end)
