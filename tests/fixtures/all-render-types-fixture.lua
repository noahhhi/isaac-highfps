-- ISOLATED COPY ONLY: 10 entity render types plus MC_POST_RENDER.
-- Source API checks: IsaacDocs EntityPlayer FireKnife/FireTear, EntityLaser
-- ShootAngle, EntityFamiliar.Player; links in ALL-RENDER-TYPES.md.
-- It controls input and spawns test entities. Render callbacks only change Lua
-- counters/data and draw colored text; they never write Position or Velocity.
local mod=RegisterMod("HighFPS isolated all render types",1)
local game=Game()
local seed="L4A6 X4X8"
local expected=Seeds.String2Seed(seed)
local active,pending,issued,done=false,false,false,false
local origin,startTime=0,0
local names={"MAIN","PLAYER","FAMILIAR","NPC","PICKUP","TEAR","PROJECTILE","LASER","KNIFE","EFFECT","BOMB"}
local colors={{1,1,1},{1,.2,.2},{.2,1,.2},{.2,.5,1},{1,1,.2},{1,.2,1},{.2,1,1},{1,.6,.2},{.6,.4,1},{.5,1,.6},{1,.5,.7}}
local counts,seen,failed,entities,spawned={},{},{},{},{}
local function frame() return game:GetFrameCount()-origin end
local function message(tag,text) Isaac.DebugString("["..tag.."] "..text) end
local function getEntity(name)
    local e=entities[name]
    return e and e:Exists() and e or nil
end
local function emit(kind)
    local parts={"kind="..kind,"f="..frame(),"t="..string.format("%.3f",(Isaac.GetTime()-startTime)/1000)}
    for _,name in ipairs(names) do
        parts[#parts+1]=name.."="..(counts[name] or 0)
        local e=getEntity(name)
        parts[#parts+1]=name.."_alive="..(e and 1 or 0)
        parts[#parts+1]=name.."_data="..(e and (e:GetData().HFRTWrites or 0) or -1)
    end
    message("HFRT",table.concat(parts," "))
end
local function trySpawn(name,fn)
    if failed[name] or getEntity(name) then return end
    local ok,e=pcall(fn)
    if not ok or not e or not e:Exists() then
        failed[name]=true
        message("HFRT_MISSING","type="..name.." reason="..tostring(ok and "spawn_returned_no_live_entity" or e):gsub("%s","_"))
        return
    end
    entities[name]=e
    e:GetData().HFRTType=name
    e:GetData().HFRTWrites=0
    spawned[name]=(spawned[name] or 0)+1
    message("HFRT_SPAWN","type="..name.." f="..frame().." ordinal="..spawned[name].." seed="..e.InitSeed)
end
local function draw(name,slot,entity)
    if not active or done then return end
    if entity then
        local target=getEntity(name)
        if not target or entity.Index~=target.Index or entity.InitSeed~=target.InitSeed then return end
        local data=entity:GetData()
        data.HFRTWrites=(data.HFRTWrites or 0)+1
    end
    counts[name]=(counts[name] or 0)+1
    if not seen[name] then
        seen[name]=true
        message("HFRT_SEEN","type="..name.." f="..frame())
    end
    local c=colors[slot]
    Isaac.RenderText(name.." "..counts[name],20,20+(slot-1)*18,c[1],c[2],c[3],1)
    Isaac.RenderText("XXXX",220+(counts[name]%30)*2,20+(slot-1)*18,c[1],c[2],c[3],1)
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function()
    if not issued then pending,active=true,false;return end
    assert(game:GetSeeds():GetStartSeed()==expected,"all-render-types seed mismatch")
    active,done=true,false
    origin,startTime=game:GetFrameCount(),Isaac.GetTime()
    counts,seen,failed,entities,spawned={},{},{},{},{}
    entities.PLAYER=Isaac.GetPlayer(0)
    entities.PLAYER:GetData().HFRTWrites=0
    for _,name in ipairs(names) do counts[name]=0 end
    message("HFTEST","started all-render-types")
    message("HFRT_META","seed="..expected.." stageSeed="..game:GetSeeds():GetStageSeed(game:GetLevel():GetStage()).." playerSeed="..Isaac.GetPlayer(0).InitSeed)
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
    local f,p=frame(),Isaac.GetPlayer(0)
    if f==5 then p.Position=Vector(320,320) end
    if f>=30 and f<300 and f%30==0 then
        trySpawn("FAMILIAR",function()
            local e=Isaac.Spawn(EntityType.ENTITY_FAMILIAR,FamiliarVariant.BROTHER_BOBBY,0,Vector(290,320),Vector(0,0),p):ToFamiliar()
            e.Player=p
            return e
        end)
        trySpawn("NPC",function()
            local e=Isaac.Spawn(EntityType.ENTITY_GAPER,0,0,Vector(500,250),Vector(0,0),nil):ToNPC()
            e:AddEntityFlags(EntityFlag.FLAG_FREEZE)
            e.EntityCollisionClass=EntityCollisionClass.ENTCOLL_NONE
            return e
        end)
        trySpawn("PICKUP",function()
            return Isaac.Spawn(EntityType.ENTITY_PICKUP,PickupVariant.PICKUP_COIN,0,Vector(100,320),Vector(0,0),nil):ToPickup()
        end)
        trySpawn("TEAR",function() return p:FireTear(Vector(160,250),Vector(0,-2),false,false,false) end)
        trySpawn("PROJECTILE",function()
            return Isaac.Spawn(EntityType.ENTITY_PROJECTILE,0,0,Vector(80,220),Vector(1,0),getEntity("NPC")):ToProjectile()
        end)
        trySpawn("LASER",function()
            assert(EntityLaser and EntityLaser.ShootAngle,"EntityLaser.ShootAngle_unavailable")
            return EntityLaser.ShootAngle(1,Vector(320,320),270,45,Vector(0,0),p)
        end)
        trySpawn("KNIFE",function() return p:FireKnife(p,0,true,0,0) end)
        trySpawn("EFFECT",function()
            return Isaac.Spawn(EntityType.ENTITY_EFFECT,EffectVariant.POOF01,0,Vector(420,350),Vector(0,0),nil):ToEffect()
        end)
        trySpawn("BOMB",function()
            local e=Isaac.Spawn(EntityType.ENTITY_BOMB,BombVariant.BOMB_NORMAL,0,Vector(80,400),Vector(0,0),p):ToBomb()
            e:SetExplosionCountdown(360) -- one spawn-time lifetime, no per-frame timer writes
            return e
        end)
    end
    if f%15==0 then emit("update") end
    if f>=330 then
        emit("done")
        for _,name in ipairs(names) do
            message("HFRT_COVERAGE","type="..name.." observed="..(seen[name] and 1 or 0).." callbacks="..counts[name].." spawned="..(spawned[name] or 0).." spawn_failed="..(failed[name] and 1 or 0))
        end
        done=true
        message("HFTEST","DONE all-render-types")
    end
end)

mod:AddCallback(ModCallbacks.MC_POST_RENDER,function() draw("MAIN",1,nil) end)
local callbackNames={PLAYER="MC_POST_PLAYER_RENDER",FAMILIAR="MC_POST_FAMILIAR_RENDER",NPC="MC_POST_NPC_RENDER",PICKUP="MC_POST_PICKUP_RENDER",TEAR="MC_POST_TEAR_RENDER",PROJECTILE="MC_POST_PROJECTILE_RENDER",LASER="MC_POST_LASER_RENDER",KNIFE="MC_POST_KNIFE_RENDER",EFFECT="MC_POST_EFFECT_RENDER",BOMB="MC_POST_BOMB_RENDER"}
for slot=2,#names do
    local name=names[slot]
    local callback=ModCallbacks[callbackNames[name]]
    if callback then
        mod:AddCallback(callback,function(_,entity) draw(name,slot,entity) end)
    else
        message("HFRT_MISSING","type="..name.." reason=callback_enum_unavailable")
    end
end
