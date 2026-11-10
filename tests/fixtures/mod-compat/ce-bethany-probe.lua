-- ISOLATED ONLY, unexecuted draft. Root enforces test silence before launch.
-- Copy ce_compat_probe.lua alongside this file renamed main.lua.
-- Does not call CE render/feedback methods, change CE settings, or draw overlays.
local P=include("ce_compat_probe")
local mod=RegisterMod("HighFPS isolated CE Bethany probe",1)
local game=Game()
local pending,seeded,active,done=false,false,false,false
local origin,updates,renders,playerRenders,damageRequests=0,0,0,0,0
local expected=Seeds.String2Seed("L4A6 X4X8")
local function emit(kind)
    local p=Isaac.GetPlayer(0)
    P.log("HFCB",{kind=kind,f=game:GetFrameCount()-origin,gf=game:GetFrameCount(),
        ui=Isaac.GetFrameCount(),u=updates,r=renders,pr=playerRenders,
        damage_requests=damageRequests,player=P.player(p)})
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    P.requireNormal()
    if not seeded then pending=true;return end
    assert(game:GetSeeds():GetStartSeed()==expected,"Bethany fixed seed failed")
    assert(Isaac.GetPlayer(0):GetPlayerType()==PlayerType.PLAYER_BETHANY,"Expected normal Bethany")
    active,done=true,false;origin=game:GetFrameCount()
    updates,renders,playerRenders,damageRequests=0,0,0,0
    Isaac.DebugString("[HFTEST] started ce-bethany")
    emit("start")
end)
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,entity,hook,action)
    if not active or done or not entity or not entity:ToPlayer() then return end
    if action<ButtonAction.ACTION_LEFT or action>ButtonAction.ACTION_DOWN then return end
    local f=game:GetFrameCount()-origin
    local yes=(f>=35 and f<50 and action==ButtonAction.ACTION_RIGHT)
        or (f>=80 and f<95 and action==ButtonAction.ACTION_LEFT)
    if hook==InputHook.GET_ACTION_VALUE then return yes and 1 or 0 end
    if hook==InputHook.IS_ACTION_PRESSED then return yes end
    if hook==InputHook.IS_ACTION_TRIGGERED then return yes and (f==35 or f==80) end
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if pending then
        pending=false;seeded=true
        P.queueSeededCharacter(PlayerType.PLAYER_BETHANY,"L4A6 X4X8")
        return
    end
    if not active or done then return end
    updates=updates+1
    local f=game:GetFrameCount()-origin;local p=Isaac.GetPlayer(0)
    if f==5 then
        local settings=P.settings()
        P.log("HFCB_SETTINGS",{settings=settings})
        P.requireEnabled(settings,"Soul Charge Shield","魂心充能护盾")
        P.requireEnabled(settings,"Shield Effects","护盾视听效果")
        p.Position=game:GetRoom():GetCenterPos();p.Velocity=Vector(0,0)
    end
    -- First fill to cap: Soul Charge's legitimate doubling cannot exceed 99.
    -- Subsequent reductions avoid injecting a gain the bonus module doubles.
    if f==10 then p:SetSoulCharge(99);emit("set_99") end
    if f==60 then p:SetSoulCharge(30);emit("set_30") end
    if f==110 then p:SetSoulCharge(4);emit("set_4") end
    if f==180 then p:SetSoulCharge(1);emit("set_1") end
    if f==130 or f==190 then
        assert(p:GetDamageCooldown()==0,"Unexpected active damage cooldown")
        local beforeCharge,beforeHealth=p:GetSoulCharge(),p:GetHearts()
        damageRequests=damageRequests+1
        emit("before_penalty_hit")
        -- One actual penalty-damage request invokes the installed shield callback.
        -- Not a feedback preview and not an extra fake hit to manufacture effects.
        p:TakeDamage(1,0,EntityRef(p),0)
        emit("after_penalty_hit")
        assert(p:GetHearts()==beforeHealth,"Shield failed to preserve red health")
        assert(p:GetSoulCharge()==math.max(0,beforeCharge-2),"Wrong shield charge cost")
        assert(p:GetDamageCooldown()>=60,"Shield cooldown missing")
    end
    emit("update")
    if f>=250 then
        done=true;emit("done")
        assert(damageRequests==2,"Expected exactly two requested penalty hits")
        Isaac.DebugString("[HFTEST] DONE ce-bethany")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if active and not done then renders=renders+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_RENDER,function(_,p)
    if active and not done and p.Index==Isaac.GetPlayer(0).Index then
        playerRenders=playerRenders+1
        if playerRenders%10==0 then emit("player_render") end
    end
end)
