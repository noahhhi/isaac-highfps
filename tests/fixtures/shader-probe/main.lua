-- ISOLATED MOD PACKAGE: install this directory, including content/shaders.xml.
-- Do not combine with another fixture that starts/restarts the test run.
local mod=RegisterMod("HighFPS isolated shader probe",1)
local game=Game()
local seed="L4A6 X4X8"
local expected=Seeds.String2Seed(seed)
local pending,issued,active,done=false,false,false,false
local origin,startTime,seq,u,r=0,0,0,0,0
local names={"HFShaderProbeA","HFShaderProbeB"}
local calls={HFShaderProbeA=0,HFShaderProbeB=0}
local params={}
local function frame() return game:GetFrameCount()-origin end
local function note(kind,name,code)
    seq=seq+1
    Isaac.DebugString(string.format("[HFSC] kind=%s name=%s seq=%d t=%.3f f=%d u=%d r=%d a=%d b=%d code=%.1f",
        kind,name or "none",seq,(Isaac.GetTime()-startTime)/1000,frame(),u,r,calls[names[1]],calls[names[2]],code or -1))
end
local function makeParams(name)
    local n=calls[name]
    local phase=(n%120)/119
    local tint=name==names[1] and {1,0.2+0.6*phase,0.2,1} or {0.2,0.3+0.5*phase,1,1}
    return {ProbeEnabled=active and 1 or 0,ProbeCode=(name==names[1] and 0 or 1000)+n,ProbeTint=tint}
end
mod:AddCallback(ModCallbacks.MC_GET_SHADER_PARAMS,function(_,name)
    if calls[name]==nil then return end
    if active and not done then
        calls[name]=calls[name]+1
        params[name]=makeParams(name)
        note("shader",name,params[name].ProbeCode)
    end
    return params[name] or makeParams(name)
end)
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    if not issued then pending,active=true,false;return end
    assert(game:GetSeeds():GetStartSeed()==expected,"shader-probe seed mismatch")
    origin,startTime,seq,u,r=game:GetFrameCount(),Isaac.GetTime(),0,0,0
    calls={HFShaderProbeA=0,HFShaderProbeB=0}
    params={}
    active,done=true,false
    Isaac.DebugString("[HFTEST] started shader-probe")
    Isaac.DebugString("[HFSC_META] seed="..expected.." names=HFShaderProbeA,HFShaderProbeB mode=screen_shader sprite_custom_shader=uncovered")
    note("start")
end)
mod:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,entity,hook,action)
    if not active or not entity or not entity:ToPlayer() then return end
    if action>=ButtonAction.ACTION_LEFT and action<=ButtonAction.ACTION_SHOOTDOWN then
        if hook==InputHook.GET_ACTION_VALUE then return 0.0 end
        return false
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if pending then
        pending,issued=false,true
        Isaac.GetPlayer(0):ChangePlayerType(PlayerType.PLAYER_ISAAC)
        Isaac.ExecuteCommand("seed "..seed)
        return
    end
    if not active or done then return end
    u=u+1
    note("update")
    if frame()>=300 then
        note("done")
        done=true
        Isaac.DebugString("[HFTEST] DONE shader-probe")
    end
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if not active or done then return end
    r=r+1
    note("render")
    Isaac.RenderText("SHADER A "..calls[names[1]].."  B "..calls[names[2]],25,150,1,1,1,1)
    Isaac.RenderText("A code="..(params[names[1]] and params[names[1]].ProbeCode or -1),25,170,1,.5,.2,1)
    Isaac.RenderText("B code="..(params[names[2]] and params[names[2]].ProbeCode or -1),25,190,.2,.5,1,1)
end)
