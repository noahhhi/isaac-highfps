-- ISOLATED SILENT PROFILE ONLY. Runner must select Normal and enforce silence.
-- Construct two native option pedestals under Curse of the Blind, then walk
-- into one. Only the native pickup/option system may remove the other pedestal.
-- This is NOT naturally rolled treasure-room generation, a shop, or directRemove.
local mod=RegisterMod("HighFPS Regret native blind option choice",1)
local game=Game();local seedText="L4A6 X4X8";local expected=Seeds.String2Seed(seedText)
local pending,issued,active,done=false,false,false,false
local origin,startedAt,chosenAt,renders=0,0,nil,0
local pair,known,initial={}, {CollectibleType.COLLECTIBLE_SAD_ONION,CollectibleType.COLLECTIBLE_INNER_EYE}, {}
local function tick()return game:GetFrameCount()-origin end
local function exists(i)return pair[i] and pair[i]:Exists() end
local function log(event)
    local p=Isaac.GetPlayer(0)
    Isaac.DebugString(string.format("[HFRPCHOICE] ev=%s t=%d gf=%d r=%d blind=%s leftExists=%s rightExists=%s inv=%d,%d pos=%.4f,%.4f",
        event,tick(),game:GetFrameCount(),renders,tostring(game:GetLevel():GetCurses()&LevelCurse.CURSE_OF_BLIND~=0),
        tostring(exists(1)),tostring(exists(2)),p:GetCollectibleNum(known[1]),p:GetCollectibleNum(known[2]),p.Position.X,p.Position.Y))
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    if not issued and game:GetSeeds():GetStartSeed()~=expected then pending=true;Isaac.DebugString("[HFV_SETUP] regret native choice seed restart");return end
    issued=true
end)
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,e,hook,action)
    if not active or done or not e or not e:ToPlayer() then return end
    if action<ButtonAction.ACTION_LEFT or action>ButtonAction.ACTION_SHOOTDOWN then return end
    local p=e:ToPlayer()
    local move=not chosenAt and tick()>=75 and tick()<180 and p:GetCollectibleNum(known[1])==initial[1]
    local pressed=move and action==ButtonAction.ACTION_UP
    if hook==InputHook.GET_ACTION_VALUE then return pressed and 1 or 0 end
    if hook==InputHook.IS_ACTION_PRESSED then return pressed end
    if hook==InputHook.IS_ACTION_TRIGGERED then return pressed and tick()==75 end
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if done then return end
    if pending then pending=false;issued=true;Isaac.ExecuteCommand("seed "..seedText);return end
    if not issued or game:GetFrameCount()<15 then return end
    local p=Isaac.GetPlayer(0)
    if not active then
        if game:GetSeeds():GetStartSeed()~=expected or game:IsGreedMode() or not game:GetRoom():IsClear() then
            Isaac.DebugString("[HFRPCHOICE] ev=FAIL reason=requires_normal_clear_seeded_start");done=true;return
        end
        if p:GetPlayerType()~=PlayerType.PLAYER_ISAAC or p:GetActiveItem()==CollectibleType.COLLECTIBLE_GLOWING_HOUR_GLASS then
            Isaac.DebugString("[HFRPCHOICE] ev=FAIL reason=requires_Isaac_without_Hourglass");done=true;return
        end
        game:GetLevel():AddCurse(LevelCurse.CURSE_OF_BLIND,false)
        local c=game:GetRoom():GetCenterPos()
        p.Position=c+Vector(-70,110);p.Velocity=Vector.Zero
        for i,item in ipairs(known)do
            initial[i]=p:GetCollectibleNum(item)
            pair[i]=Isaac.Spawn(EntityType.ENTITY_PICKUP,PickupVariant.PICKUP_COLLECTIBLE,item,c+Vector(i==1 and -70 or 70,40),Vector.Zero,nil):ToPickup()
            pair[i].OptionsPickupIndex=71
        end
        origin=game:GetFrameCount();startedAt=Isaac.GetTime();active=true
        Isaac.DebugString("[HFTEST] started regret-native-choice")
        log("setup_constructed_blind_native_options")
    end
    if tick()==60 then
        if not exists(1)or not exists(2)or pair[1]:GetSprite():GetAnimation()~="Idle"or pair[2]:GetSprite():GetAnimation()~="Idle"then
            log("FAIL_pedestals_not_idle_before_choice");done=true;return
        end
        log("ready_both_idle")
    end
    if not chosenAt and p:GetCollectibleNum(known[1])>initial[1]then chosenAt=tick();log("native_left_acquired") end
    if tick()%5==0 then log("tick") end
    if chosenAt and tick()==chosenAt+1 then log("native_option_removal_check") end
    if chosenAt and tick()>=chosenAt+75 then
        if exists(2)or p:GetCollectibleNum(known[2])~=initial[2]then log("FAIL_native_option_not_removed")
        else log("DONE_native_choice_and_unpicked_removal");Isaac.DebugString("[HFTEST] DONE regret-native-choice") end
        done=true;active=false
    elseif not chosenAt and tick()>=180 then log("FAIL_no_native_pickup");done=true;active=false end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if active and not done then
        renders=renders+1
        if Isaac.GetTime()-startedAt>15000 then log("FAIL_wall_timeout");done=true;active=false end
    end
end)
