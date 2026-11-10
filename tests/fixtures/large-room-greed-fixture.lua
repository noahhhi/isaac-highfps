-- ISOLATED TEST MOD ONLY. Never deploy to the user's live mods/saves.
-- Select native Greed (Difficulty2) and Isaac in the isolated character menu.
-- Uses MC_INPUT_ACTION, not a virtual controller. No wave button, shooting,
-- network action, item use, room-shape edit or repeated position correction.
local mod = RegisterMod("HighFPS isolated Greed large-room camera", 1)
local game = Game()
local config = {seed="L4A6 X4X8", duration=720, setupTimeout=180, margin=80}
local expectedSeed = Seeds.String2Seed(config.seed)
local issued, pending, preparing, active, done, failed = false,false,false,false,false,false
local setupTicks, transitionRequested, roomIndex = 0,false,nil
local origin, startTime, updates, playerUpdates, mainCount, playerRenders = 0,0,0,0,0,0
local inputQueries, appliedDirection, previousPhase, reverseFromMotion = 0,0,nil,false
local laneX, topY, bottomY, bounds = nil,nil,nil,nil
local minY,maxY,minScrollY,maxScrollY = nil,nil,nil,nil
local scrollSupported = nil

local phases = {
    {0,30,"settle",0},
    {30,150,"down_long_1",1},
    {150,180,"stop_bottom_1",0},
    {180,300,"up_long_1",-1},
    {300,330,"stop_top_1",0},
    {330,350,"down_before_reverse",1},
    {350,370,"up_immediate_reverse",-1},
    {370,400,"stop_after_reverse",0},
    {400,520,"down_long_2",1},
    {520,550,"stop_bottom_2",0},
    {550,670,"up_long_2",-1},
    {670,720,"settle_end",0},
}
local function frame() return game:GetFrameCount()-origin end
local function phase(f)
    for _,s in ipairs(phases) do if f>=s[1] and f<s[2] then return s[3],s[4],s[1] end end
    return "done",0,config.duration
end
local function isGreed()
    local ok,value=pcall(function() return game:IsGreedMode() end)
    return ok and value==true
end
local function fail(reason)
    if failed then return end
    failed,done=true,true
    pending,preparing=false,false
    appliedDirection=0
    Isaac.DebugString("[HFLG_FAIL] reason="..reason)
    -- No DONE marker: the generic runner must not treat this case as passing.
    Isaac.DebugString("[HFV_SETUP] large-room-greed INVALID "..reason)
end
local function readScroll(room)
    if scrollSupported==false then return nil end
    local ok,v=pcall(function() return room:GetRenderScrollOffset() end)
    if ok and v and type(v.X)=="number" and type(v.Y)=="number" then
        scrollSupported=true;return v
    end
    scrollSupported=false;return nil
end
local function readScreen(room,position)
    local ok,v=pcall(function() return room:WorldToScreenPosition(position) end)
    if ok and v then return v end
    return nil
end
local function append(parts,key,value)
    if type(value)=="number" then value=string.format("%.6f",value) end
    parts[#parts+1]=key.."="..tostring(value):gsub("%s","_")
end
local function note(kind)
    if not active then return end
    local p,room=Isaac.GetPlayer(0),game:GetRoom()
    local name,direction=phase(frame())
    local scroll,screen=readScroll(room),readScreen(room,p.Position)
    local fields={kind=kind,t=(Isaac.GetTime()-startTime)/1000,f=frame(),gf=game:GetFrameCount(),
        ui=Isaac.GetFrameCount(),tc=game.TimeCounter,u=updates,pu=playerUpdates,
        main=mainCount,pr=playerRenders,iq=inputQueries,phase=name,requested=direction,applied=appliedDirection,
        room=game:GetLevel():GetCurrentRoomIndex(),rf=room:GetFrameCount(),shape=room:GetRoomShape(),
        room_type=room:GetType(),greed=isGreed() and 1 or 0,difficulty=game.Difficulty,
        paused=game:IsPaused() and 1 or 0,px=p.Position.X,py=p.Position.Y,vx=p.Velocity.X,vy=p.Velocity.Y,
        speed=p.MoveSpeed,lane=laneX,y_top=topY,y_bottom=bottomY,
        scroll_available=scroll and 1 or 0,scroll_x=scroll and scroll.X or -99999,scroll_y=scroll and scroll.Y or -99999,
        screen_available=screen and 1 or 0,screen_x=screen and screen.X or -99999,screen_y=screen and screen.Y or -99999}
    local ok,count=pcall(function() return room:GetAliveEnemiesCount() end)
    fields.enemies=ok and count or -1
    if scroll and kind=="update" then
        minScrollY=minScrollY and math.min(minScrollY,scroll.Y) or scroll.Y
        maxScrollY=maxScrollY and math.max(maxScrollY,scroll.Y) or scroll.Y
    end
    local keys,parts={}, {"[HFLG]"}
    for key in pairs(fields) do keys[#keys+1]=key end
    table.sort(keys)
    for _,key in ipairs(keys) do append(parts,key,fields[key]) end
    Isaac.DebugString(table.concat(parts," "))
end
local function roomBounds(room)
    local ok,tl,br=pcall(function() return room:GetTopLeftPos(),room:GetBottomRightPos() end)
    if not ok or not tl or not br then return nil end
    if br.Y-tl.Y<400 then return nil end -- normal1x1 height is insufficient
    return {left=tl.X,right=br.X,top=tl.Y,bottom=br.Y}
end
local function chooseLane(room,b)
    local first,last=b.top+config.margin,b.bottom-config.margin
    -- Stay off the central Greed wave button and the centered room exits.
    for _,fraction in ipairs({.25,.75,.35,.65}) do
        local x=b.left+(b.right-b.left)*fraction
        local clear=true
        for y=first,last,20 do
            local ok,collision=pcall(function() return room:GetGridCollisionAtPos(Vector(x,y)) end)
            if not ok or collision~=0 then clear=false;break end
        end
        if clear then return x,first,last end
    end
    return nil
end
local function prepare()
    local room,p=game:GetRoom(),Isaac.GetPlayer(0)
    if game:IsPaused() or room:GetFrameCount()<5 then return end
    local b=roomBounds(room)
    if not b then
        if transitionRequested then return end
        -- If Greed starts in an adjacent small room, enter the already-generated
        -- tall room through its real door. Do not fabricate a room with goto.
        for slot=0,7 do
            local door=room:GetDoor(slot)
            if door and door.TargetRoomIndex>=0 then
                local desc=game:GetLevel():GetRoomByIdx(door.TargetRoomIndex,-1)
                local shape=desc and desc.Data and desc.Data.Shape
                if desc and desc.Data and desc.Data.Type==RoomType.ROOM_DEFAULT and
                   (shape==RoomShape.ROOMSHAPE_1x2 or shape==RoomShape.ROOMSHAPE_2x2) then
                    transitionRequested=true
                    Isaac.DebugString("[HFV_SETUP] large-room-greed enter_native_room="..door.TargetRoomIndex)
                    p.Position,p.Velocity=door.Position,Vector(0,0)
                    game:StartRoomTransition(door.TargetRoomIndex,slot%4,RoomTransitionAnim.WALK,p,-1)
                    return
                end
            end
        end
        fail("no_native_tall_greed_room_or_bounds_api");return
    end
    laneX,topY,bottomY=chooseLane(room,b)
    if not laneX then fail("no_clear_offcenter_vertical_lane");return end
    if not isGreed() then fail("not_greed_after_setup");return end
    bounds=b
    roomIndex=game:GetLevel():GetCurrentRoomIndex()
    -- Single placement before the measured window. Never correct positions while
    -- moving; collisions or mod movement differences remain observable.
    p.Position,p.Velocity=Vector(laneX,topY),Vector(0,0)
    origin,startTime=game:GetFrameCount(),Isaac.GetTime()
    updates,playerUpdates,mainCount,playerRenders,inputQueries=0,0,0,0,0
    appliedDirection,previousPhase,reverseFromMotion=0,nil,false
    minY,maxY=p.Position.Y,p.Position.Y
    minScrollY,maxScrollY=nil,nil
    active,preparing,done=true,false,false
    Isaac.DebugString("[HFTEST] started large-room-greed")
    Isaac.DebugString(string.format("[HFLG_META] seed=%u stageSeed=%u playerSeed=%u difficulty=%d stage=%d room=%d shape=%d bounds=%.3f,%.3f,%.3f,%.3f lane=%.3f y_top=%.3f y_bottom=%.3f duration=%d",
        expectedSeed,game:GetSeeds():GetStageSeed(game:GetLevel():GetStage()),p.InitSeed,game.Difficulty,
        game:GetLevel():GetStage(),roomIndex,room:GetRoomShape(),b.left,b.top,b.right,b.bottom,laneX,topY,bottomY,config.duration))
    note("start")
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    if not isGreed() then fail("select_native_greed_difficulty_2_in_isolated_menu");return end
    if not issued and game:GetSeeds():GetStartSeed()~=expectedSeed then
        pending=true
        Isaac.DebugString("[HFV_SETUP] large-room-greed seed restart")
        return
    end
    issued=true
    if game:GetSeeds():GetStartSeed()~=expectedSeed then fail("seed_mismatch");return end
    if Isaac.GetPlayer(0):GetPlayerType()~=PlayerType.PLAYER_ISAAC then fail("select_isaac_in_isolated_menu");return end
    preparing,setupTicks,transitionRequested=true,0,false
end)
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,entity,hook,action)
    if not entity or not entity:ToPlayer() or entity.Index~=Isaac.GetPlayer(0).Index then return end
    if not (active or preparing or pending or failed) then return end
    if action<ButtonAction.ACTION_LEFT or action>ButtonAction.ACTION_SHOOTDOWN then return end
    inputQueries=inputQueries+1
    local direction,begin=0,0
    if active and not done and not failed then
        local _,wanted,at=phase(frame());direction,begin=wanted,at
        local y=Isaac.GetPlayer(0).Position.Y
        if (direction>0 and y>=bottomY) or (direction<0 and y<=topY) then direction=0 end
    end
    appliedDirection=direction
    local pressed=(direction>0 and action==ButtonAction.ACTION_DOWN) or
                  (direction<0 and action==ButtonAction.ACTION_UP)
    if hook==InputHook.GET_ACTION_VALUE then return pressed and 1.0 or 0.0 end
    if hook==InputHook.IS_ACTION_PRESSED then return pressed end
    if hook==InputHook.IS_ACTION_TRIGGERED then return pressed and frame()==begin end
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if failed then return end
    if pending then
        pending,issued=false,true
        Isaac.GetPlayer(0):ChangePlayerType(PlayerType.PLAYER_ISAAC)
        Isaac.ExecuteCommand("seed "..config.seed)
        return
    end
    if preparing then
        setupTicks=setupTicks+1
        if setupTicks>config.setupTimeout then fail("native_room_setup_timeout");return end
        prepare();return
    end
    if not active or done then return end
    if not isGreed() or game:GetLevel():GetCurrentRoomIndex()~=roomIndex then fail("mode_or_room_changed_during_motion");return end
    updates=updates+1
    local p=Isaac.GetPlayer(0)
    minY,maxY=math.min(minY,p.Position.Y),math.max(maxY,p.Position.Y)
    local name=phase(frame())
    if name~=previousPhase then
        if name=="up_immediate_reverse" and math.abs(p.Velocity.Y)>.1 then reverseFromMotion=true end
        note("phase_change");previousPhase=name
    end
    note("update")
    if frame()>=config.duration then
        local travel=maxY-minY
        local scrollRange=minScrollY and maxScrollY-minScrollY or -1
        local enoughTravel=travel>=math.min(300,(bottomY-topY)*.7)
        Isaac.DebugString(string.format("[HFLG_COVERAGE] travel_y=%.6f scroll_y_range=%.6f reverse_nonzero=%d long_travel=%d scroll_available=%d main=%d u=%d",
            travel,scrollRange,reverseFromMotion and 1 or 0,enoughTravel and 1 or 0,scrollSupported and 1 or 0,mainCount,updates))
        if not enoughTravel or not reverseFromMotion then fail("motion_coverage_incomplete");return end
        if scrollSupported and scrollRange<1 then fail("camera_scroll_not_exercised");return end
        note("done");done=true;appliedDirection=0
        Isaac.DebugString("[HFTEST] DONE large-room-greed")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE,function(_,p)
    if active and not done and not failed and p.Index==Isaac.GetPlayer(0).Index then playerUpdates=playerUpdates+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_RENDER,function(_,p)
    if active and not done and not failed and p.Index==Isaac.GetPlayer(0).Index then playerRenders=playerRenders+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if not active or done or failed then return end
    mainCount=mainCount+1
    note("main")
    local name=phase(frame())
    Isaac.RenderText("GREED CAMERA "..name.." f="..frame().." MAIN="..mainCount,20,30,1,1,1,1)
end)
