-- READ-ONLY PAUSE OBSERVER. No input hook, no pause command, no seed/restart,
-- no state writes. Root sends the real pause/resume key to the isolated display.
-- Ready is emitted after 30 world updates. Done follows a real observed pause
-- and resume plus two wall seconds of observation.
local mod=RegisterMod("HighFPS isolated pause observer",1)
local game=Game()
local active,done,armed,sawPause=false,false,false,false
local startTime,origin,u,r,pu,resumedAt=0,0,0,0,0,nil
local lastPaused=nil
local function note(kind)
    Isaac.DebugString(string.format("[HFP] kind=%s t=%.3f gf=%d f=%d ui=%d tc=%d rf=%d paused=%d armed=%d u=%d r=%d pu=%d",
        kind,(Isaac.GetTime()-startTime)/1000,game:GetFrameCount(),game:GetFrameCount()-origin,
        Isaac.GetFrameCount(),game.TimeCounter,game:GetRoom():GetFrameCount(),game:IsPaused() and 1 or 0,
        armed and 1 or 0,u,r,pu))
end
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    active,done,armed,sawPause=true,false,false,false
    startTime,origin=Isaac.GetTime(),game:GetFrameCount()
    u,r,pu,resumedAt=0,0,0,nil
    lastPaused=game:IsPaused()
    Isaac.DebugString("[HFTEST] started pause-observer")
    Isaac.DebugString("[HFP_META] seed="..game:GetSeeds():GetStartSeed().." mode=read_only_external_pause_input")
    note("start")
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if not active or done then return end
    u=u+1
    if not armed and u>=30 then
        armed=true
        lastPaused=game:IsPaused()
        note("ready")
        Isaac.DebugString("[HFPAUSE_READY] send actual pause key to isolated display")
    end
    note("update")
end)
mod:AddCallback(ModCallbacks.MC_POST_PLAYER_UPDATE,function(_,p)
    if active and not done and p.Index==Isaac.GetPlayer(0).Index then pu=pu+1 end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if not active or done then return end
    r=r+1
    local paused=game:IsPaused()
    if paused~=lastPaused then
        lastPaused=paused
        note(paused and "pause_enter" or "pause_exit")
        if armed and paused then sawPause=true end
        if armed and sawPause and not paused then resumedAt=Isaac.GetTime() end
    end
    if r%6==0 then note("render") end
    if resumedAt and not paused and Isaac.GetTime()-resumedAt>=2000 then
        note("done")
        done=true
        Isaac.DebugString("[HFTEST] DONE pause-observer")
    end
end)
