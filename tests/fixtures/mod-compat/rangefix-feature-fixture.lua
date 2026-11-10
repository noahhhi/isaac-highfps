-- ISOLATED, SILENT TEST PROFILE ONLY. Enforce silence in launcher before launch.
-- Set CASE to forgotten / sword / bag; run each as a separate seeded case.
-- Genuine native player attacks create all knives. No knife is manually spawned.
-- Target contact damage is zero; player/weapon damage and cooldowns are untouched.
local CASE="forgotten"
local mod=RegisterMod("HighFPS RangeFix natural weapons",1)
local game=Game()
local seedText="L4A6 X4X8"
local expected=Seeds.String2Seed(seedText)
local pending,issued,active,done=false,false,false,false
local start,updates,renders,hits=0,0,0,0
local target,origin=nil,nil
local serial,ids=0,{}
local function log(s) Isaac.DebugString("[HFRANGE] case="..CASE.." "..s) end
local function tick() return game:GetFrameCount()-start end
local function id(e)
    local h=GetPtrHash(e)
    if not ids[h] then serial=serial+1;ids[h]=serial end
    return ids[h]
end
local function knife(e,where)
    local k=e:ToKnife();if not k then return end
    local p=Isaac.GetPlayer(0)
    log(string.format("ev=%s t=%d gf=%d id=%d v=%d sub=%d ef=%d pos=%.5f,%.5f vel=%.5f,%.5f scale=%.6f ss=%.6f,%.6f rot=%.5f target=%.5f,%.5f dist=%.5f size=%.5f range=%.5f",
        where,tick(),game:GetFrameCount(),id(k),k.Variant,k.SubType,k.FrameCount,k.Position.X,k.Position.Y,
        k.Velocity.X,k.Velocity.Y,k.Scale,k.SpriteScale.X,k.SpriteScale.Y,k.Rotation,
        k.TargetPosition.X,k.TargetPosition.Y,k.Position:Distance(p.Position),k.Size,p.TearRange))
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    if not issued and game:GetSeeds():GetStartSeed()~=expected then
        pending=true;Isaac.DebugString("[HFV_SETUP] rangefix seed restart");return
    end
    issued=true
end)
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,e,hook,action)
    if not active or done or not e or not e:ToPlayer() then return end
    if action<ButtonAction.ACTION_LEFT or action>ButtonAction.ACTION_SHOOTDOWN then return end
    local t=tick();local cycle=(t-30)%90
    local fire=t>=30 and t<270 and cycle<60 and action==ButtonAction.ACTION_SHOOTRIGHT
    if hook==InputHook.GET_ACTION_VALUE then return fire and 1.0 or 0.0 end
    if hook==InputHook.IS_ACTION_PRESSED then return fire end
    if hook==InputHook.IS_ACTION_TRIGGERED then return fire and cycle==0 end
end)
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_INIT,function(_,k)
    if active and not done then knife(k,"init") end
end)
mod:AddCallback(ModCallbacks.MC_POST_KNIFE_UPDATE,function(_,k)
    if active and not done then knife(k,"knife_update") end
end)
mod:AddCallback(ModCallbacks.MC_ENTITY_TAKE_DMG,function(_,e,amount,flags,source)
    if active and e.Type==EntityType.ENTITY_PLAYER then
        local s=source and source.Entity
        log(string.format("ev=player_damage t=%d amount=%.5f source=%d,%d,%d",tick(),amount,
            s and s.Type or -1,s and s.Variant or -1,s and s.SubType or -1))
    end
    if active and target and GetPtrHash(e)==GetPtrHash(target) then
        hits=hits+1;local s=source and source.Entity
        log(string.format("ev=hit t=%d n=%d amount=%.5f hp=%.5f source=%d,%d,%d",
            tick(),hits,amount,e.HitPoints,s and s.Type or -1,s and s.Variant or -1,s and s.SubType or -1))
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if done then return end
    if pending then pending=false;issued=true;Isaac.ExecuteCommand("seed "..seedText);return end
    if not issued or game:GetFrameCount()<15 then return end
    local p=Isaac.GetPlayer(0)
    if not active then
        if game:GetSeeds():GetStartSeed()~=expected then log("FAIL seed_mismatch");done=true;return end
        if not RangeFixForBonesAndSword then log("FAIL actual_RangeFix_missing");done=true;return end
        if game:IsGreedMode() or not game:GetRoom():IsClear() then
            log("ev=FAIL reason=requires_normal_clear_start_room");done=true;return
        end
        p:ChangePlayerType(CASE=="forgotten" and PlayerType.PLAYER_THEFORGOTTEN or PlayerType.PLAYER_ISAAC)
        if CASE=="sword" then p:AddCollectible(CollectibleType.COLLECTIBLE_SPIRIT_SWORD,0,false)
        elseif CASE=="bag" then p:AddCollectible(CollectibleType.COLLECTIBLE_BAG_OF_CRAFTING,0,false,ActiveSlot.SLOT_PRIMARY)
        elseif CASE~="forgotten" then log("FAIL unknown_case");done=true;return end
        origin=game:GetRoom():GetCenterPos()+Vector(-70,0);p.Position=origin;p.Velocity=Vector.Zero
        target=Isaac.Spawn(EntityType.ENTITY_GAPER,0,0,origin+Vector(65,0),Vector.Zero,nil):ToNPC()
        target.MaxHitPoints=100000;target.HitPoints=100000
        target.CollisionDamage=0
        assert(target.CollisionDamage==0,"Native target CollisionDamage setter failed")
        target:AddEntityFlags(EntityFlag.FLAG_FREEZE | EntityFlag.FLAG_NO_KNOCKBACK | EntityFlag.FLAG_NO_PHYSICS_KNOCKBACK)
        start=game:GetFrameCount();active=true
        local rf=RangeFixForBonesAndSword
        log(string.format("ev=start gf=%d enabled=%s factor=%s boc=%s bocfactor=%s range=%.5f",
            start,tostring(rf:IsEnabled()),tostring(rf:GetRangeFactor()),tostring(rf:IsEnabledBOC()),tostring(rf:GetRangeFactorBOC()),p.TearRange))
        log("ev=target_setup collisionDamage="..target.CollisionDamage.." playerCooldownUnchanged=true")
        Isaac.DebugString("[HFTEST] started rangefix-"..CASE)
    end
    updates=updates+1
    if target and target:Exists() then target.CollisionDamage=0 end
    -- A real use of an equipped Bag enters its native swing mode; attack input
    -- then creates the native subtype/variant. No direct Knife constructor/API.
    if CASE=="bag" and (tick()==30 or tick()==120 or tick()==210) then
        p:UseActiveItem(CollectibleType.COLLECTIBLE_BAG_OF_CRAFTING,0,ActiveSlot.SLOT_PRIMARY)
        log("ev=native_bag_use t="..tick())
    end
    if tick()%15==0 then log(string.format("ev=tick t=%d u=%d r=%d hits=%d knives=%d player=%.5f,%.5f hp=%.5f playerHearts=%d playerSoul=%d playerBone=%d targetPos=%.5f,%.5f collisionDamage=%.2f",tick(),updates,renders,hits,serial,p.Position.X,p.Position.Y,target and target.HitPoints or -1,p:GetHearts(),p:GetSoulHearts(),p:GetBoneHearts(),target.Position.X,target.Position.Y,target.CollisionDamage)) end
    if tick()>=300 then
        log(string.format("ev=DONE t=%d u=%d r=%d hits=%d knives=%d",tick(),updates,renders,hits,serial))
        Isaac.DebugString("[HFTEST] DONE rangefix-"..CASE);done=true;active=false
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if not active or done then return end
    if Isaac.GetPlayer(0):IsDead() then
        log("ev=FAIL reason=player_dead worldClockStopped=true");done=true;active=false;return
    end
    renders=renders+1
    -- Observe final post-update values after every mod had a chance to run.
    for _,e in ipairs(Isaac.FindByType(EntityType.ENTITY_KNIFE,-1,-1,false,false)) do knife(e,"main") end
end)
