-- ISOLATED ONLY. Draft: never run on physical saves. Copy shared helper too.
-- mode="starting" validates real Eden initialization/rewind/fade.
-- mode="blessing_and_starting" first earns ONE reward by real Blessing pickup,
-- then starts seeded Eden and expects two independent three-choice groups.
-- Change caseToken for each run or remove ONLY this fixture's isolated save.
local config={mode="starting",caseToken="ce-eden-start-v1",seed="L4A6 X4X8"}
local P=include("ce_compat_probe")
local json=require("json")
local mod=RegisterMod("HighFPS isolated CE Eden probe",1)
local game=Game()
local expected=Seeds.String2Seed(config.seed)
local state={token=config.caseToken,mode=config.mode,phase="bootstrap",updates=0,renders=0,starts=0}
if mod:HasData() then
    local old=json.decode(mod:LoadData())
    if old.token==config.caseToken then state=old end
end
assert(state.phase~="done","Terminal case token reused; clear only owned isolated fixture data")
local pendingBootstrap=false
local blessing
local bootstrapUpdates=0
local function persist() mod:SaveData(json.encode(state)) end
local function emit(kind,extra)
    P.log("HFCE",{kind=kind,phase=state.phase,mode=config.mode,token=config.caseToken,
        u=state.updates,r=state.renders,starts=state.starts,gf=game:GetFrameCount(),
        ui=Isaac.GetFrameCount(),seed=game:GetSeeds():GetStartSeed(),
        player=P.player(Isaac.GetPlayer(0)),extra=extra})
end
local function requestEden()
    assert(not state.issuedTarget,"Never issue target restart twice")
    assert(game:GetSeeds():GetStartSeed()~=expected,
        "Bootstrap seed equals target; CE needs a genuinely new seed for starting initialization")
    state.issuedTarget=true;state.phase="target_wait";persist()
    emit("request_seeded_eden")
    P.queueSeededCharacter(PlayerType.PLAYER_EDEN,config.seed)
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function(_,continued)
    P.requireNormal()
    if state.issuedTarget and game:GetSeeds():GetStartSeed()==expected then
        assert(Isaac.GetPlayer(0):GetPlayerType()==PlayerType.PLAYER_EDEN,"Not native normal Eden")
        state.starts=state.starts+1
        assert(state.starts<=4,"Unexpected repeated startup/continuation loop")
        state.phase="target";persist()
        emit("game_started",{continued=continued})
        if state.starts==1 then Isaac.DebugString("[HFTEST] started ce-eden") end
    elseif state.phase=="bootstrap" then
        pendingBootstrap=true
    else
        emit("unexpected_start",{continued=continued})
        error("Unexpected seed/state after target restart")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if pendingBootstrap then
        pendingBootstrap=false
        assert(Isaac.GetPlayer(0):GetPlayerType()==PlayerType.PLAYER_ISAAC,
            "Start fixture in fresh Isaac bootstrap; do not reuse an Eden run")
        local settings=P.settings();P.log("HFCE_SETTINGS",{settings=settings})
        P.requireEnabled(settings,"Starting Passive Choice","初始被动三选一")
        P.requireEnabled(settings,"Eden's Blessing Choice","伊甸的祝福三选一")
        assert(P.pendingRewards()==0,"Isolated CE profile must begin with no pending rewards")
        assert(#P.pickups()==0,"Unexpected bootstrap pickups; use a clean new Isaac start")
        if config.mode=="starting" then requestEden();return end
        assert(config.mode=="blessing_and_starting","Unknown fixture mode")
        state.phase="acquire_blessing";persist()
        emit("bootstrap_ready",{note="bootstrap seed not normalized; target Eden is seeded"})
    end
    if state.phase=="acquire_blessing" then
        bootstrapUpdates=bootstrapUpdates+1
        local p=Isaac.GetPlayer(0)
        if bootstrapUpdates==10 then
            assert(p:GetCollectibleNum(CollectibleType.COLLECTIBLE_EDENS_BLESSING)==0,"Bootstrap already owns Blessing")
            blessing=Isaac.Spawn(EntityType.ENTITY_PICKUP,PickupVariant.PICKUP_COLLECTIBLE,
                CollectibleType.COLLECTIBLE_EDENS_BLESSING,game:GetRoom():GetCenterPos(),Vector(0,0),nil):ToPickup()
            emit("spawn_real_blessing",{id=blessing.SubType,seed=blessing.InitSeed})
        end
        if bootstrapUpdates>=45 and bootstrapUpdates<90 and blessing and blessing:Exists() then
            p.Position=blessing.Position;p.Velocity=Vector(0,0)
        end
        if bootstrapUpdates%5==0 then
            emit("acquire_observe",{pending=P.pendingRewards(),blessing_count=p:GetCollectibleNum(CollectibleType.COLLECTIBLE_EDENS_BLESSING)})
        end
        if P.pendingRewards()==1 and p:GetCollectibleNum(CollectibleType.COLLECTIBLE_EDENS_BLESSING)==1 then
            state.rewardEarned=true;state.phase="reward_wait";state.rewardWall=Isaac.GetTime();persist()
            emit("reward_earned_once")
        end
        assert(bootstrapUpdates<180,"Real Blessing pickup did not earn exactly one reward")
    elseif state.phase=="reward_wait" then
        assert(P.pendingRewards()==1,"Pending reward changed before new run")
        if Isaac.GetTime()-state.rewardWall>1500 then requestEden() end
    elseif state.phase=="target" then
        state.updates=state.updates+1
        if state.updates%5==0 then emit("update",{pickups=P.pickups()}) end
        if state.updates%30==0 then persist() end
        if state.updates==120 then
            local rows=P.pickups();local groups={};local ids={}
            for _,p in ipairs(rows) do
                if p.options>0 then
                    groups[p.options]=(groups[p.options] or 0)+1
                    assert(not ids[p.id],"Duplicate choice ID across groups")
                    ids[p.id]=true
                end
            end
            local n=0
            for _,count in pairs(groups) do assert(count==3,"Incomplete option group");n=n+1 end
            local wanted=config.mode=="starting" and 1 or 2
            assert(n==wanted,"Expected actual starting/blessing three-choice groups")
            assert(P.pendingRewards()==0,"Rewards not consumed exactly once")
            emit("choice_groups_verified",{groups=n,pickups=rows,inventory=P.inventory(Isaac.GetPlayer(0))})
        end
        if state.updates>=180 then
            state.phase="done";persist();emit("done")
            Isaac.DebugString("[HFTEST] DONE ce-eden")
        end
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if state.phase=="target" then
        state.renders=state.renders+1
        if state.renders<=100 or state.renders%20==0 then emit("main_render") end
    end
end)
