-- ISOLATED SILENT PROFILE ONLY; launcher enforces music/SFX/voice volume zero.
-- Spawns native slots, walks into them through native player input, and observes
-- actual TimeMachine accel_slot calls. Never calls accel_slot/step itself.
-- Each native slot uses an explicit Game:Spawn seed. Three spatially separated
-- positions preserve payouts while preventing cross-phase pickup contamination.
-- Approach from below: horizontal collision lands exactly on the mod's strict
-- circular contact boundary; vertical native collision is measured explicitly.
local mod=RegisterMod("HighFPS TimeMachine native contact",1)
local game=Game();local seedText="L4A6 X4X8";local expected=Seeds.String2Seed(seedText)
local pending,issued,active,done,wrapped=false,false,false,false,false
local start,updates,renders,calls,ranCalls,extraSlot,extraPlayer=0,0,0,0,0,0,0
local machine,origin,lastPhase,startRoom=nil,nil,nil,nil
local findCalls,lastTargetCount,guardCalls,requestedSuccessfulUpdates,startTimeCounter=0,0,0,0,0
local function log(s) Isaac.DebugString("[HFTM] "..s) end
local function tick() return game:GetFrameCount()-start end
local function phase(t)
    if t<30 then return "approach",true end
    if t<120 then return "contact_coins",true end
    if t<210 then return "no_coins",true end
    if t<270 then return "leave",false end
    return "low_hp_guard",true
end
local function snapshot(event)
    local p=Isaac.GetPlayer(0);local s=machine and machine:Exists() and machine or nil
    log(string.format("ev=%s t=%d gf=%d tc=%d u=%d r=%d phase=%s coins=%d hp=%d dc=%d slot=%d ef=%d anim=%s af=%d dist=%.4f contact=%s calls=%d ran=%d slotExtra=%d playerExtra=%d pos=%.4f,%.4f psize=%.4f ssize=%.4f findCalls=%d targets=%d guardCalls=%d requestedSuccessfulUpdates=%d clockExtra=%d slotSeed=%s",
        event,tick(),game:GetFrameCount(),game.TimeCounter,updates,renders,phase(tick()),p:GetNumCoins(),p:GetHearts()+p:GetSoulHearts(),p:GetDamageCooldown(),
        s and s.Variant or -1,s and s.FrameCount or -1,s and s:GetSprite():GetAnimation() or "none",s and s:GetSprite():GetFrame() or -1,
        s and p.Position:Distance(s.Position) or -1,tostring(s and p.Position:Distance(s.Position)<p.Size+s.Size or false),
        calls,ranCalls,extraSlot,extraPlayer,p.Position.X,p.Position.Y,p.Size,s and s.Size or -1,findCalls,lastTargetCount,guardCalls,requestedSuccessfulUpdates,game.TimeCounter-startTimeCounter-tick(),s and tostring(s.InitSeed) or "none"))
end
local function wrap()
    if wrapped then return true end
    if not(tmmc and tmmc.istmmcfixed and tmmc.accel_slot) then return false end
    local originalFind=tmmc.find_targets
    tmmc.find_targets=function(self,...)
        local list=originalFind(self,...)
        if active then findCalls=findCalls+1;lastTargetCount=#list end
        return list
    end
    local original=tmmc.accel_slot
    tmmc.accel_slot=function(self,p,s,n,modded)
        local sf,pf=s.FrameCount,p.FrameCount
        local hp=p:GetHearts()+p:GetSoulHearts()
        local result=original(self,p,s,n,modded)
        if active then
            calls=calls+1;if result then ranCalls=ranCalls+1;requestedSuccessfulUpdates=requestedSuccessfulUpdates+n end
            if s.Variant==2 and hp<=1 and result==false then guardCalls=guardCalls+1 end
            local ds,dp=s.FrameCount-sf,p.FrameCount-pf;extraSlot=extraSlot+ds;extraPlayer=extraPlayer+dp
            log(string.format("ev=accel t=%d v=%d requested=%d result=%s slotDelta=%d playerDelta=%d hpBefore=%d hpAfter=%d",
                tick(),s.Variant,n,tostring(result),ds,dp,hp,p:GetHearts()+p:GetSoulHearts()))
        end
        return result
    end
    wrapped=true;return true
end
local function place(variant,seed,offset)
    -- Keep all previous machines and their real outputs. Later phases occupy a
    -- distant area, so a Dollar/coin/bomb payout cannot be collected by resetting
    -- the player onto the same spot. No payout/entity is removed by the fixture.
    origin=game:GetRoom():GetCenterPos()+offset
    local p=Isaac.GetPlayer(0)
    machine=game:Spawn(EntityType.ENTITY_SLOT,variant,origin,Vector.Zero,nil,0,seed)
    assert(machine and machine:Exists() and machine.InitSeed==seed,"Native Game:Spawn seed contract failed")
    p.Position=origin+Vector(0,45);p.Velocity=Vector.Zero
    log(string.format("ev=spawn_slot t=%d variant=%d requestedSeed=%d actualSeed=%d pos=%.3f,%.3f",
        active and tick() or 0,variant,seed,machine.InitSeed,origin.X,origin.Y))
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    if not issued and game:GetSeeds():GetStartSeed()~=expected then pending=true;Isaac.DebugString("[HFV_SETUP] timemachine seed restart");return end
    issued=true
end)
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,e,hook,action)
    if not active or done or not e or not e:ToPlayer() then return end
    if action<ButtonAction.ACTION_LEFT or action>ButtonAction.ACTION_SHOOTDOWN then return end
    local _,toward=phase(tick())
    local p=e:ToPlayer()
    local live=machine and machine:Exists()
    local moveUp=toward and live and p.Position.Y>origin.Y-35
    local moveDown=not toward and p.Position.Y<origin.Y+110
    local pressed=moveUp and action==ButtonAction.ACTION_UP or moveDown and action==ButtonAction.ACTION_DOWN
    if hook==InputHook.GET_ACTION_VALUE then return pressed and 1.0 or 0.0 end
    if hook==InputHook.IS_ACTION_PRESSED then return pressed end
    if hook==InputHook.IS_ACTION_TRIGGERED then return false end
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if done then return end
    if pending then pending=false;issued=true;Isaac.ExecuteCommand("seed "..seedText);return end
    if not issued or game:GetFrameCount()<15 then return end
    local p=Isaac.GetPlayer(0)
    if not active then
        if game:GetSeeds():GetStartSeed()~=expected or not wrap() then log("FAIL seed_or_actual_TimeMachine_missing");done=true;return end
        if game:IsGreedMode() or not game:GetRoom():IsClear() then
            log("ev=FAIL reason=requires_normal_clear_start_room");done=true;return
        end
        p:ChangePlayerType(PlayerType.PLAYER_ISAAC)
        p:AddCoins(50-p:GetNumCoins())
        place(1,0x541001,Vector(-120,-60));start=game:GetFrameCount();startTimeCounter=game.TimeCounter;startRoom=game:GetLevel():GetCurrentRoomIndex();active=true
        log(string.format("ev=start max=%s accel=%s prevent=%s slot1=%s slot2=%s clear=%s",
            tostring(tmmc.speedmax),tostring(tmmc.speeda),tostring(tmmc.preventDeath),tostring(tmmc.enable[1]),tostring(tmmc.enable[2]),tostring(game:GetRoom():IsClear())))
        Isaac.DebugString("[HFTEST] started timemachine-native")
    end
    if game:GetLevel():GetCurrentRoomIndex()~=startRoom then
        log("FAIL unexpected_room_change");done=true;active=false;return
    end
    updates=updates+1
    local name=phase(tick())
    if name~=lastPhase then
        if name=="no_coins" then p:AddCoins(-p:GetNumCoins());place(1,0x541002,Vector(120,-60))
        elseif name=="low_hp_guard" then
            p:AddSoulHearts(-p:GetSoulHearts());p:AddHearts(1-p:GetHearts())
            -- Explicitly protect against a *native* lethal donation while testing
            -- the mod's low-HP acceleration guard. No effect invincibility added.
            p:SetMinDamageCooldown(600);place(2,0x541003,Vector(120,60))
        end
        lastPhase=name;snapshot("phase")
    end
    if tick()%10==0 then snapshot("tick") end
    if tick()>=360 then
        if ranCalls==0 or requestedSuccessfulUpdates==0 or game.TimeCounter-startTimeCounter<=tick() or guardCalls==0 or p:GetHearts()+p:GetSoulHearts()<=0 then
            snapshot("FAIL_unexercised_or_dead")
            Isaac.DebugString("[HFTEST] FAIL timemachine-native coverage")
        else snapshot("DONE");Isaac.DebugString("[HFTEST] DONE timemachine-native") end
        active=false;done=true
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function() if active and not done then renders=renders+1 end end)
