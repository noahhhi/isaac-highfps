-- ISOLATED ONLY, unexecuted draft. External process-local XInput owns all input.
-- No MC_INPUT_ACTION handler, synthetic raw values, mod-filter calls or drawing.
-- Requires copied ce_compat_probe.lua for read-only state/log helpers.
local P=include("ce_compat_probe")
local mod=RegisterMod("HighFPS isolated controller chain probe",1)
local game=Game()
local pending,seeded,active,done,ready=false,false,false,false,false
local origin,readyUpdate,updates,renders,lastSampleTick=0,0,0,0,-1
local slotChanges,presses,releases,nonzeroSamples,laserBirths=0,0,0,0,0
local previousSlots,previousR3=nil,false
local setupController
local expected=Seeds.String2Seed("L4A6 X4X8")
local actions={
    {"shoot_left",ButtonAction.ACTION_SHOOTLEFT},{"shoot_right",ButtonAction.ACTION_SHOOTRIGHT},
    {"shoot_up",ButtonAction.ACTION_SHOOTUP},{"shoot_down",ButtonAction.ACTION_SHOOTDOWN},
    {"move_left",ButtonAction.ACTION_LEFT},{"move_right",ButtonAction.ACTION_RIGHT},
    {"move_up",ButtonAction.ACTION_UP},{"move_down",ButtonAction.ACTION_DOWN}}
local function configSnapshot()
    local rows={};local menu=P.menu()
    assert(menu and menu.MenuData,"MCM needed to observe current Optimizer switches")
    for _,cat in ipairs(menu.MenuData) do
        if cat.Name=="Controller Optimizer" then
            for _,sub in ipairs(cat.Subcategories or {}) do
                for _,opt in ipairs(sub.Options or {}) do
                    if type(opt.CurrentSetting)=="function" then
                        rows[#rows+1]={label=type(opt.Display)=="function" and opt.Display() or opt.Display,
                            value=opt.CurrentSetting()}
                    end
                end
            end
        end
    end
    local enabled,swap=false,false
    for _,row in ipairs(rows) do
        if row.label:find("Enable optimizer:",1,true)==1 then enabled=row.value end
        if row.label:find("Schoolbag quick swap:",1,true)==1 then swap=row.value end
    end
    assert(enabled==true and swap==true,"Optimizer and Schoolbag swap must already be enabled")
    local registered=0
    for _,cb in ipairs(Isaac.GetCallbacks(ModCallbacks.MC_INPUT_ACTION)) do
        if cb.Mod and cb.Mod.Name=="Controller Optimizer" then registered=registered+1 end
    end
    assert(registered>=3,"Optimizer native input callbacks absent")
    P.log("HFCO_CONFIG",{options=rows,registered_input_callbacks=registered,
        mcm_open_controller=menu.Config["Mod Config Menu"].OpenMenuController,
        fixed_source_contract={version="1.7.3",analog360=true,active_swap_button=13},
        contract_note="Fixed source defaults require matching installed source hash; not private runtime introspection"})
end
local function sample(kind)
    local p=Isaac.GetPlayer(0);local controller=p.ControllerIndex
    assert(controller>0 and controller==setupController,"Controller ownership changed or became keyboard")
    local vals={}
    for _,entry in ipairs(actions) do
        local value=Input.GetActionValue(entry[2],controller)
        vals[entry[1]]={value=value,pressed=Input.IsActionPressed(entry[2],controller),
            triggered=Input.IsActionTriggered(entry[2],controller)}
        if entry[1]:find("shoot_",1,true)==1 and value>0.001 then nonzeroSamples=nonzeroSamples+1 end
    end
    local stick=p:GetShootingJoystick()
    local r3=Input.IsButtonPressed(13,controller)
    local r3Trigger=Input.IsButtonTriggered(13,controller)
    if r3 and not previousR3 then presses=presses+1 end
    if not r3 and previousR3 then releases=releases+1 end
    previousR3=r3
    local first=p:GetActiveItem(ActiveSlot.SLOT_PRIMARY)
    local second=p:GetActiveItem(ActiveSlot.SLOT_SECONDARY)
    local slots=tostring(first)..":"..tostring(second)
    if previousSlots and previousSlots~=slots then
        slotChanges=slotChanges+1
        P.log("HFCO_SWAP",{from=previousSlots,to=slots,gf=game:GetFrameCount(),ui=Isaac.GetFrameCount(),
            r3=r3,r3_triggered=r3Trigger,presses=presses,releases=releases,slot_changes=slotChanges})
    end
    previousSlots=slots
    local menu=P.menu()
    P.log("HFCO",{kind=kind,u=updates,r=renders,gf=game:GetFrameCount(),ui=Isaac.GetFrameCount(),
        controller=controller,actions=vals,shoot_x=stick.X,shoot_y=stick.Y,
        r3=r3,r3_triggered=r3Trigger,primary=first,secondary=second,slot_changes=slotChanges,
        presses=presses,releases=releases,laser_births=laserBirths,
        mcm_visible=menu and menu.IsVisible or false,player=P.player(p)})
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    P.requireNormal()
    local p=Isaac.GetPlayer(0)
    assert(game:GetNumPlayers()==1 and p.ControllerIndex>0,
        "Need one P1 owned by real engine XInput path; never assign ControllerIndex")
    if not seeded then pending=true;setupController=p.ControllerIndex;return end
    assert(game:GetSeeds():GetStartSeed()==expected,"Fixed seed restart failed")
    assert(p.ControllerIndex==setupController,"Seed restart changed native controller ownership")
    assert(p:GetPlayerType()==PlayerType.PLAYER_AZAZEL,"Expected normal Azazel")
    active=true;origin=game:GetFrameCount()
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if pending then
        pending=false;seeded=true
        P.queueSeededCharacter(PlayerType.PLAYER_AZAZEL,"L4A6 X4X8")
        return
    end
    if not active or done then return end
    updates=updates+1
    local p=Isaac.GetPlayer(0)
    if updates==5 then
        configSnapshot()
        p:AddCollectible(CollectibleType.COLLECTIBLE_ANALOG_STICK,0,false)
        p:AddCollectible(CollectibleType.COLLECTIBLE_SCHOOLBAG,0,false)
        p:AddCollectible(CollectibleType.COLLECTIBLE_BOOK_OF_SHADOWS,0,false,ActiveSlot.SLOT_PRIMARY)
        p:AddCollectible(CollectibleType.COLLECTIBLE_D6,0,false,ActiveSlot.SLOT_SECONDARY)
        assert(p:HasCollectible(CollectibleType.COLLECTIBLE_ANALOG_STICK),"Analog Stick not installed")
        assert(p:HasCollectible(CollectibleType.COLLECTIBLE_SCHOOLBAG),"Schoolbag not installed")
        assert(p:GetActiveItem(ActiveSlot.SLOT_PRIMARY)==CollectibleType.COLLECTIBLE_BOOK_OF_SHADOWS
            and p:GetActiveItem(ActiveSlot.SLOT_SECONDARY)==CollectibleType.COLLECTIBLE_D6,"Two active slots not prepared")
        p.Position=game:GetRoom():GetCenterPos();p.Velocity=Vector(0,0)
        assert(not (P.menu() and P.menu().IsVisible),"MCM must start closed")
    end
    if updates==15 then
        ready=true;readyUpdate=updates
        Isaac.DebugString("[HFTEST] started controller-native-chain")
        P.log("HFCO_READY",{controller=p.ControllerIndex,seed=game:GetSeeds():GetStartSeed(),
            scene="Azazel + Analog Stick + Schoolbag + two distinct actives",inventory=P.inventory(p)})
        sample("ready")
    end
    if ready and updates-readyUpdate>=600 then
        sample("done");done=true
        assert(nonzeroSamples>0,"No observed stick response: reject unexercised input case")
        assert(presses==4 and releases==4,"Expected four actual R3 press/release pulses")
        assert(not (P.menu() and P.menu().IsVisible),"MCM remained open after four pulses")
        P.log("HFCO_FINAL",{slot_changes=slotChanges,presses=presses,releases=releases,
            nonzero_action_samples=nonzeroSamples,laser_births=laserBirths,
            note="Swap outcome and laser behavior must match native same-mod baseline, not a guessed isolated-filter result"})
        Isaac.DebugString("[HFTEST] DONE controller-native-chain")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if not ready or done then return end
    renders=renders+1
    local tick=Isaac.GetFrameCount()
    -- Bound observer queries to one sample per Manager tick; never manufacture
    -- additional raw input polls in repeated render callbacks at the same tick.
    if tick~=lastSampleTick then lastSampleTick=tick;sample("canonical_observe") end
end)
mod:AddCallback(ModCallbacks.MC_POST_LASER_INIT,function(_,laser)
    if not ready or done then return end
    laserBirths=laserBirths+1
    P.log("HFCO_LASER",{ordinal=laserBirths,seed=laser.InitSeed,variant=laser.Variant,
        angle=laser.Angle,gf=game:GetFrameCount(),ui=Isaac.GetFrameCount(),
        parent_type=laser.Parent and laser.Parent.Type,spawner_type=laser.SpawnerType})
end)
