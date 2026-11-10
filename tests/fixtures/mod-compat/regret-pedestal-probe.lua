-- DRAFT: install ONLY in the owned isolated validation mod. Never executed here.
-- Runner must enforce zero test audio before launch. This fixture does not
-- modify any mod configuration or save, nor does it paint a substitute overlay.
-- Direct removal exercises the installed Regret Pedestals disappearing-item path.
-- It is not a native Curse of the Blind / option-choice gameplay test.
local mod = RegisterMod("HighFPS isolated Regret probe", 1)
local game = Game()
local expectedSeed = Seeds.String2Seed("L4A6 X4X8")
local pending, seeded, active, done = false, false, false, false
local origin, startTime, renders, updates = 0, 0, 0, 0
local owned = {}
local function log(kind, extra)
    Isaac.DebugString(string.format("[HFRP] kind=%s f=%d t=%.6f u=%d r=%d %s",
        kind, game:GetFrameCount()-origin, (Isaac.GetTime()-startTime)/1000,
        updates, renders, extra or ""))
end
local function spawnPair()
    local center = game:GetRoom():GetCenterPos()
    for i,id in ipairs({CollectibleType.COLLECTIBLE_SAD_ONION, CollectibleType.COLLECTIBLE_INNER_EYE}) do
        local p = Isaac.Spawn(EntityType.ENTITY_PICKUP, PickupVariant.PICKUP_COLLECTIBLE,
            id, center+Vector(i==1 and -70 or 70, 50), Vector(0,0), nil):ToPickup()
        owned[i]=p
        log("spawn", string.format("ordinal=%d id=%d seed=%d x=%.3f y=%.3f",
            i,p.SubType,p.InitSeed,p.Position.X,p.Position.Y))
    end
end
local function removeOne(i)
    local p=owned[i]
    assert(p and p:Exists(), "Probe pickup vanished before scheduled removal")
    log("remove",string.format("ordinal=%d id=%d anim=%s",i,p.SubType,p:GetSprite():GetAnimation()))
    assert(p:GetSprite():GetAnimation()=="Idle", "Regret registration requires Idle; reject uncovered setup")
    p:Remove()
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    if not seeded then pending=true;return end
    assert(game:GetSeeds():GetStartSeed()==expectedSeed,"Fixed seed restart failed")
    local p=Isaac.GetPlayer(0)
    assert(p:GetPlayerType()==PlayerType.PLAYER_ISAAC,"Probe requires Isaac")
    assert(p:GetActiveItem()~=CollectibleType.COLLECTIBLE_GLOWING_HOUR_GLASS,
        "Regret defaults to disabled with Glowing Hourglass")
    origin,startTime,renders,updates=game:GetFrameCount(),Isaac.GetTime(),0,0
    owned={};active=true;done=false
    p.ControlsEnabled=false
    p.Position=game:GetRoom():GetCenterPos()+Vector(0,100)
    p.Velocity=Vector(0,0)
    Isaac.DebugString("[HFTEST] started regret-probe")
    log("start","seed="..expectedSeed.." scenario=direct_removal_not_blind_curse")
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if pending then
        pending=false;seeded=true
        Isaac.GetPlayer(0):ChangePlayerType(PlayerType.PLAYER_ISAAC)
        Isaac.ExecuteCommand("seed L4A6 X4X8")
        return
    end
    if not active or done then return end
    updates=updates+1
    local f=game:GetFrameCount()-origin
    if f==15 then spawnPair() end
    if f==45 then removeOne(1) end
    if f==105 then removeOne(2) end
    if f==165 then spawnPair() end
    if f==195 then removeOne(1);removeOne(2) end
    if f%5==0 then log("update") end
    if f>=255 then
        done=true;Isaac.GetPlayer(0).ControlsEnabled=true
        log("done");Isaac.DebugString("[HFTEST] DONE regret-probe")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if active and not done then renders=renders+1 end
end)
