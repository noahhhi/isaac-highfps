-- Offline fake API harness. No game process, input, or save directory is opened.
local fixture=arg[1] or "tests/fixtures/rewind-fixture.lua"
local registry,saved,encoded,serial={},nil,{},0
local seed,roomIndex,roomFrame,gf,ui,wall=123,84,0,0,0,1000
local seedCommands,uses,doneCount=0,0,0
local reloadOnUse=true
local function clone(t)
    if type(t)~="table" then return t end
    local out={};for k,v in pairs(t) do out[k]=clone(v) end;return out
end
package.preload.json=function() return {
    encode=function(t) serial=serial+1;encoded[tostring(serial)]=clone(t);return tostring(serial) end,
    decode=function(s) return clone(assert(encoded[s])) end
} end
ModCallbacks=setmetatable({},{__index=function(_,k)return k end})
PlayerType={PLAYER_ISAAC=0};CollectibleType={COLLECTIBLE_GLOWING_HOUR_GLASS=422}
UseFlag={USE_OWNED=8};ActiveSlot={SLOT_PRIMARY=0};RoomType={ROOM_DEFAULT=1};RoomTransitionAnim={WALK=0}
ButtonAction={ACTION_LEFT=0,ACTION_SHOOTDOWN=7};InputHook={GET_ACTION_VALUE=0}
Seeds={String2Seed=function(s)assert(s=="L4A6 X4X8");return 1500427799 end}
local player={Index=1,InitSeed=3754715714,Position={X=320,Y=380}}
function player:GetPlayerType() return 0 end
function player:ChangePlayerType(t) assert(t==0) end
function player:AddCollectible(item) assert(item==422) end
local level={}
function level:GetCurrentRoomIndex() return roomIndex end
function level:GetStage() return 1 end
function level:GetRoomByIdx() return {Data={Type=1}} end
local room={}
function room:GetFrameCount() return roomFrame end
function room:GetDoor(slot) if slot==0 then return {TargetRoomIndex=71,Position={X=320,Y=120}} end end
local seeds={}
function seeds:GetStartSeed() return seed end
function seeds:GetStageSeed() return 1473973499 end
local game={TimeCounter=0}
function game:GetFrameCount() return gf end
function game:GetSeeds() return seeds end
function game:GetLevel() return level end
function game:GetRoom() return room end
function game:IsPaused() return false end
function game:StartRoomTransition(target)
    roomIndex,roomFrame=target,0
    registry.MC_POST_NEW_ROOM()
end
function Game() return game end
Isaac={GetPlayer=function()return player end,GetTime=function()return wall end,GetFrameCount=function()return ui end,
    DebugString=function(s)if s=="[HFTEST] DONE rewind" then doneCount=doneCount+1 end end}
function Isaac.ExecuteCommand(command)
    assert(command=="seed L4A6 X4X8")
    seedCommands=seedCommands+1;seed=1500427799;gf,roomIndex,roomFrame=0,84,0
    registry.MC_POST_GAME_STARTED()
end
function RegisterMod(name)
    assert(name=="HighFPS isolated rewind lifecycle")
    registry={}
    return {AddCallback=function(_,id,fn)registry[id]=fn end,
            HasData=function()return saved~=nil end,LoadData=function()return saved end,
            SaveData=function(_,value)saved=value end}
end
function player:UseActiveItem(item,flags,slot)
    assert(item==422 and flags==8 and slot==0)
    uses=uses+1
    local durable=require("json").decode(saved)
    assert(durable.phase=="rewind_requested" and durable.used)
    roomIndex,roomFrame,gf=84,0,12 -- deliberately rewind the absolute game clock
    if reloadOnUse then
        dofile(fixture)
        registry.MC_POST_GAME_STARTED()
    end
end
local function tick()
    wall=wall+33.333;gf=gf+1;ui=ui+2;roomFrame=roomFrame+1;game.TimeCounter=gf
    registry.MC_POST_UPDATE()
    registry.MC_POST_RENDER()
end
dofile(fixture);registry.MC_POST_GAME_STARTED()
for _=1,90 do tick() end
local durable=require("json").decode(saved)
assert(seedCommands==1,"seed command repeated after VM reload")
assert(uses==1,"hourglass use repeated after VM reload")
assert(doneCount==1 and durable.phase=="done","rewind did not complete after backwards game clock")
assert(durable.resumes==1,"VM replacement did not restore the durable phase")
dofile(fixture);registry.MC_POST_GAME_STARTED()
for _=1,20 do tick() end
assert(seedCommands==1 and uses==1 and doneCount==1,"terminal token automatically restarted the fixture")
print("PASS: durable use latch precedes call; VM replacement resumes once; backwards game clock completes; reused token cannot restart")
