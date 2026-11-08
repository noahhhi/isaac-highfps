-- ISOLATED TEST ONLY. Fixed-seed, 15-second global-speed census.
-- Natural player shooting, tear flight/falling/animation and bomb timing are
-- observed without resetting their timers. Only input and initial placement
-- are controlled; one bomb is spawned far from the player's path at f90.
local mod = RegisterMod("HighFPS isolated global speed", 1)
local game = Game()
local config = {seed="L4A6 X4X8", duration=450}
local expected = Seeds.String2Seed(config.seed)
local pending, issued, active, done = false, false, false, false
local origin, startTime = 0, 0
local u, pu, tu, r, pr, tr, shots, newTears = 0,0,0,0,0,0,0,0
local tear, bomb, bombCounterReader
local bombSeed=-1
local bombUpdates, bombRemoved, bombExplodeSeen = 0, false, false
local function frame() return game:GetFrameCount()-origin end
local function append(parts, key, value)
    if type(value)=="number" then value=string.format("%.6f", value) end
    parts[#parts+1]=key.."="..tostring(value):gsub("%s", "_")
end
local function log(kind)
    if not active then return end
    local p=Isaac.GetPlayer(0)
    local fields={kind=kind,t=(Isaac.GetTime()-startTime)/1000,f=frame(),gf=game:GetFrameCount(),
        tc=game.TimeCounter,paused=game:IsPaused() and 1 or 0,room=game:GetLevel():GetCurrentRoomIndex(),
        u=u,pu=pu,tu=tu,r=r,pr=pr,tr=tr,shots=shots,newtears=newTears,
        px=p.Position.X,py=p.Position.Y,pvx=p.Velocity.X,pvy=p.Velocity.Y,
        fireDelay=p.FireDelay,maxFireDelay=p.MaxFireDelay,paf=p:GetSprite():GetFrame(),
        ta=0,tid=-1,tx=-99999,ty=-99999,tvx=-99999,tvy=-99999,th=-99999,tfall=-99999,tf=-1,taf=-1,
        ba=0,bc=-99999,bc_available=0,bf=-1,baf=-1,bu=bombUpdates,br=bombRemoved and 1 or 0}
    if tear and tear:Exists() then
        fields.ta,fields.tid=1,tear.InitSeed
        fields.tx,fields.ty,fields.tvx,fields.tvy=tear.Position.X,tear.Position.Y,tear.Velocity.X,tear.Velocity.Y
        fields.th,fields.tfall,fields.tf,fields.taf=tear.Height,tear.FallingSpeed,tear.FrameCount,tear:GetSprite():GetFrame()
        fields.tanim=tear:GetSprite():GetAnimation()
    end
    if bomb and bomb:Exists() then
        fields.ba,fields.bf,fields.baf=1,bomb.FrameCount,bomb:GetSprite():GetFrame()
        fields.banim=bomb:GetSprite():GetAnimation()
        if bombCounterReader then fields.bc,fields.bc_available=bombCounterReader(bomb),1 end
    end
    local keys={}
    for k in pairs(fields) do keys[#keys+1]=k end
    table.sort(keys)
    local parts={"[HFG]"}
    for _,k in ipairs(keys) do append(parts,k,fields[k]) end
    Isaac.DebugString(table.concat(parts," "))
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    if not issued then pending,active=true,false;return end
    assert(game:GetSeeds():GetStartSeed()==expected,"global-speed seed mismatch")
    assert(Isaac.GetPlayer(0):GetPlayerType()==PlayerType.PLAYER_ISAAC,"global-speed player mismatch")
    active,done=true,false
    origin,startTime=game:GetFrameCount(),Isaac.GetTime()
    u,pu,tu,r,pr,tr,shots,newTears=0,0,0,0,0,0,0,0
    tear,bomb,bombCounterReader=nil,nil,nil
    bombSeed=-1
    bombUpdates,bombRemoved,bombExplodeSeen=0,false,false
    Isaac.DebugString("[HFTEST] started global-speed")
    Isaac.DebugString("[HFG_META] seed="..expected.." stageSeed="..game:GetSeeds():GetStageSeed(game:GetLevel():GetStage()).." playerSeed="..Isaac.GetPlayer(0).InitSeed)
    log("start")
end)

mod:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,entity,hook,action)
    if not active or not entity or not entity:ToPlayer() then return end
    if action<ButtonAction.ACTION_LEFT or action>ButtonAction.ACTION_SHOOTDOWN then return end
    local f=frame()
    local phase=(f-60)%120
    local pressed=not done and f>=60 and f<420 and
        ((phase<35 and action==ButtonAction.ACTION_RIGHT) or
         (phase>=50 and phase<85 and action==ButtonAction.ACTION_LEFT) or
         action==ButtonAction.ACTION_SHOOTUP)
    if hook==InputHook.GET_ACTION_VALUE then return pressed and 1.0 or 0.0 end
    if hook==InputHook.IS_ACTION_PRESSED then return pressed end
    if hook==InputHook.IS_ACTION_TRIGGERED then
        return pressed and ((action==ButtonAction.ACTION_SHOOTUP and f==60) or
            (action==ButtonAction.ACTION_RIGHT and phase==0) or
            (action==ButtonAction.ACTION_LEFT and phase==50))
    end
end)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if pending then
        pending,issued=false,true
        Isaac.GetPlayer(0):ChangePlayerType(PlayerType.PLAYER_ISAAC)
        Isaac.ExecuteCommand("seed "..config.seed)
        return
    end
    if not active or done then return end
    u=u+1
    local f=frame()
    if f==5 then
        local p=Isaac.GetPlayer(0)
        p.Position,p.Velocity=Vector(160,280),Vector(0,0)
    end
    if f==30 then log("warmup_done") end
    if f==90 then
        bomb=Isaac.Spawn(EntityType.ENTITY_BOMB,BombVariant.BOMB_NORMAL,0,Vector(480,140),Vector(0,0),Isaac.GetPlayer(0)):ToBomb()
        bombSeed=bomb.InitSeed
        -- Vanilla Lua does not document a countdown getter. Detect optional
        -- extensions once, and explicitly mark missing measurement otherwise.
        local ok,value=pcall(function() return bomb:GetExplosionCountdown() end)
        if ok and type(value)=="number" then
            bombCounterReader=function(b) return b:GetExplosionCountdown() end
        else
            ok,value=pcall(function() return bomb.ExplosionCountdown end)
            if ok and type(value)=="number" then bombCounterReader=function(b) return b.ExplosionCountdown end end
        end
        log("bomb_spawn")
    end
    log("update")
    if f>=config.duration then
        log("done");done=true
        Isaac.DebugString("[HFTEST] DONE global-speed")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE,function(_,p)
    if active and not done and p.Index==Isaac.GetPlayer(0).Index then pu=pu+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_TEAR_UPDATE,function()
    if active and not done then tu=tu+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if active and not done then r=r+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_RENDER,function(_,p)
    if active and not done and p.Index==Isaac.GetPlayer(0).Index then pr=pr+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_TEAR_RENDER,function()
    if active and not done then tr=tr+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_TEAR_INIT,function()
    if active and not done then newTears=newTears+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_FIRE_TEAR,function(_,t)
    if not active or done then return end
    shots,tear=shots+1,t
    log("shot")
end)
mod:AddCallback(ModCallbacks.MC_POST_BOMB_UPDATE,function(_,b)
    if not active or done or not bomb or b.InitSeed~=bombSeed then return end
    bombUpdates=bombUpdates+1
    if not bombExplodeSeen and b:GetSprite():IsPlaying("Explode") then
        bombExplodeSeen=true;log("bomb_explode_animation")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE,function(_,entity)
    if active and not done and bomb and entity.InitSeed==bombSeed then
        bombRemoved=true;log("bomb_removed")
    end
end)
