-- ISOLATED TEST MOD ONLY. Never deploy in the user's real game/profile.
-- Launcher MUST run work/enforce-test-silence.py AFTER copying the isolated
-- profile and BEFORE launch (music, SFX, online voice volume all zero).
-- Restarts to a fixed seed, holds ACTION_MAP twice, and draws a control strip.
-- No movement, directional cursor commands, mouse clicks, or teleport requests.
-- Disable other automatic seed/input fixtures. Keep the real GoodTrip mod enabled.
local mod = RegisterMod("HighFPS GoodTrip feature observer", 1)
local game = Game()
local seedText, seedValue = "L4A6 X4X8", Seeds.String2Seed("L4A6 X4X8")
local pending, issued, active, done = false, false, false, false
local startFrame, startRoom, renders, updates = 0, nil, 0, 0
local counts = {map=0,ui=0,tab=0,input=0,inputNil=0,inputMap=0}
local installed, strip, announced = false, nil, false
local function log(s) Isaac.DebugString("[HFGT] "..s) end
local function frame() return game:GetFrameCount()-startFrame end
local function phase(t)
    if t<60 then return "baseline",false end
    if t<240 then return "hold1",true end
    if t<300 then return "released",false end
    if t<480 then return "hold2",true end
    return "final_released",false
end
local function installObserver()
    if installed then return true end
    if not (gt and gt.isgtfixedfixed and gt.get_config and gt.draw_minimap and gt.draw_minimap_ui) then return false end
    for name,key in pairs({draw_minimap="map",draw_minimap_ui="ui",tab_action="tab"}) do
        local original = gt[name]
        local counterKey = key
        gt[name] = function(...)
            counts[counterKey]=counts[counterKey]+1
            return original(...)
        end
    end
    strip=Sprite();strip:Load("sprite/gt/minimap0.anm2",true)
    installed=true
    return true
end
local function note(event)
    local cfg=gt and gt.get_config and gt:get_config() or {}
    local p=Isaac.GetPlayer(0)
    local _,want=phase(frame())
    local eligible=gt and gt.check_teleble and gt:check_teleble(false) or false
    local rooms=game:GetLevel():GetRooms();local visible=0
    for i=0,rooms.Size-1 do if rooms:Get(i).DisplayFlags>0 then visible=visible+1 end end
    log(string.format("ev=%s t=%d gf=%d r=%d u=%d phase=%s want=%s input=%s eligible=%s clear=%s cfg=%s lost=%s room=%d shape=%d visible=%d map=%d ui=%d tab=%d query=%d nilq=%d mapq=%d pos=%.3f,%.3f",
        event,frame(),game:GetFrameCount(),renders,updates,phase(frame()),tostring(want),
        tostring(Input.IsActionPressed(ButtonAction.ACTION_MAP,p.ControllerIndex)),tostring(eligible),
        tostring(game:GetLevel():GetCurrentRoomDesc().Clear),tostring(cfg.KeyboardMapEnable),tostring(cfg.FollowCurseOfLost),
        game:GetLevel():GetCurrentRoomIndex(),game:GetRoom():GetRoomShape(),visible,
        counts.map,counts.ui,counts.tab,counts.input,counts.inputNil,counts.inputMap,p.Position.X,p.Position.Y))
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    if not issued and game:GetSeeds():GetStartSeed()~=seedValue then
        pending=true;Isaac.DebugString("[HFV_SETUP] goodtrip feature seed restart");return
    end
    issued=true
    if game:GetSeeds():GetStartSeed()~=seedValue then log("FAIL seed_mismatch");done=true;return end
    active=false;done=false;announced=false;startRoom=nil;renders=0;updates=0
end)
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,entity,hook,action)
    if not active or done then return end
    if entity then
        local p=entity:ToPlayer()
        if not p or p.Index~=Isaac.GetPlayer(0).Index then return end
    end
    -- Direct Lua Input.IsActionPressed bypasses MC_INPUT_ACTION on J374.
    -- Let the verified isolated X11 Tab controller supply the real map input.
    if action==ButtonAction.ACTION_MAP then return end
    if action<ButtonAction.ACTION_LEFT or action>ButtonAction.ACTION_SHOOTDOWN then return end
    counts.input=counts.input+1
    if not entity then counts.inputNil=counts.inputNil+1 end
    if action==ButtonAction.ACTION_MAP then counts.inputMap=counts.inputMap+1 end
    local _,hold=phase(frame())
    local pressed=action==ButtonAction.ACTION_MAP and hold
    if hook==InputHook.GET_ACTION_VALUE then return pressed and 1.0 or 0.0 end
    if hook==InputHook.IS_ACTION_PRESSED then return pressed end
    -- The trigger is held across the boundary world tick, consistently in both
    -- builds. This tests sustained display, not exact hardware edge semantics.
    if hook==InputHook.IS_ACTION_TRIGGERED then return pressed and (frame()==60 or frame()==300) end
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if done then return end
    if pending then pending=false;issued=true;Isaac.ExecuteCommand("seed "..seedText);return end
    if not issued then return end
    if not active then
        if not installObserver() then
            if not announced then log("WAIT GoodTrip_not_loaded");announced=true end
            return
        end
        if game:GetFrameCount()<15 then return end
        startFrame=game:GetFrameCount();startRoom=game:GetLevel():GetCurrentRoomIndex();active=true
        for k in pairs(counts) do counts[k]=0 end
        Isaac.DebugString("[HFTEST] started goodtrip-feature")
        note("start")
        local cfg=gt:get_config()
        log(string.format("config x=%s y=%s curses=%d greed=%s fastCursor=%s quicklyMove=%s",
            tostring(cfg.TopLeftX or 100),tostring(cfg.TopLeftY or 100),game:GetLevel():GetCurses(),
            tostring(game:IsGreedMode()),tostring(cfg.FasterCursorMove),tostring(cfg.QuicklyOneRoomMove)))
    end
    updates=updates+1
    if game:GetLevel():GetCurrentRoomIndex()~=startRoom then
        note("FAIL_unexpected_room_change");done=true;return
    end
    if frame()%15==0 then note("tick") end
    if frame()>=540 then
        note("done");done=true;active=false
        if counts.map==0 then log("FAIL map_not_exercised");return end
        Isaac.DebugString("[HFTEST] DONE goodtrip-feature")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if not active or done then return end
    renders=renders+1
    -- Independent coverage of GoodTrip's reused-Sprite / changed-frame pattern.
    -- Does not invoke GoodTrip draw/step functions or bypass its display conditions.
    local animations={"RoomOutline","RoomVisited","RoomUnvisited","RoomCurrent"}
    for i,name in ipairs(animations) do
        strip:SetFrame(name,0)
        strip.Color=Color(1,i==2 and 0.4 or 1,i==3 and 0.4 or 1,1,0,0,0)
        strip:Render(Vector(280+i*24,220),Vector(0,0),Vector(0,0))
    end
    strip.Color=Color(1,1,1,1,0,0,0)
    Isaac.RenderText("GT DRAW CONTROL",300,205,1,1,1,1)
    if renders%30==0 then note("render") end
end)
