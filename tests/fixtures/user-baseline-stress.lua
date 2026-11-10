-- ISOLATED CONTINUE-ONLY performance probe. Never deploy into the real game.
-- Launcher must clone the exact saved run AND mod data, and mute isolated music,
-- SFX and voice output before launch. Characters/seeds are never changed.
-- Items stay unchanged unless cfg.extraCollectible(s) explicitly requests additions.
-- At the first world update, normalize the single player once to (80,280),
-- velocity zero: vanilla Continue chooses different entry doors across launches.
-- No effect/tear/laser is directly spawned, removed, capped or modified here.
-- Phase names describe commanded input; Marked can keep firing after release.
local probe=RegisterMod("HighFPS saved-run natural-fire census",1)
local json=require("json")
local game=Game()
local cfg={expectedSeed="XM33 7GZY",settle=90,fire=360,drain=150,maxWallMs=150000,normalizeFirstUpdatePosition={80,280},extraCollectible=nil,extraCollectibles=nil,observeAim=false}
local active,done=false,false
local origin,startWall,windowWall=0,0,0
local updates,renders,inputQueries=0,0,0
local roomIndex,startSeed
local lastUpdateGF,lastUpdateUI,lastMainGF,lastMainUI=-1,-1,-1,-1
local lastPhase=nil
local total={pre={},init={},remove={}}
local window={pre={},init={},remove={},brackets={},samples={}}
local knownEffects={}
local shotBirths=0
-- Optional observation does not query Input or synthesize any extra action.
-- J374 GetAimDirection is a cached vector getter (42D4D0: lea eax,[ecx+1630];ret).
-- GetShootingJoystick may query MC_INPUT_ACTION, so keep its original census-only use.
local aimTraceCount,aimTraceSuppressed=0,0
local targetTraceCount,targetTraceSuppressed=0,0
local lastAimSignature,lastUpdateFireDelay=nil,nil
local debugShotParents={}
local targetVariant=type(EffectVariant)=="table" and EffectVariant.TARGET or nil
-- Read the running game's enum table once; do not assume numeric effect names.
local effectVariantLabels,observedEffectNames={},{}
for name,id in pairs(type(EffectVariant)=="table" and EffectVariant or {}) do
    if type(name)=="string" and type(id)=="number" then
        local k=tostring(id);effectVariantLabels[k]=effectVariantLabels[k] or {}
        table.insert(effectVariantLabels[k],name)
    end
end
for id,names in pairs(effectVariantLabels) do
    table.sort(names);effectVariantLabels[id]=table.concat(names,"|")
end
local function observeEffectVariant(variant)
    local k=tostring(variant)
    if not observedEffectNames[k] then observedEffectNames[k]=effectVariantLabels[k] or "<not_in_EffectVariant>" end
end
local function emit(kind,data)
    data=data or {};data.kind=kind;data.gf=game:GetFrameCount();data.ui=Isaac.GetFrameCount()
    data.wall_ms=Isaac.GetTime();data.elapsed_ms=data.wall_ms-startWall
    data.tick=data.gf-origin;data.u=updates;data.r=renders
    Isaac.DebugString("[HFPERF] "..json.encode(data))
end
local function phase()
    local t=game:GetFrameCount()-origin
    if t<cfg.settle then return "settle" end
    if t<cfg.settle+cfg.fire then return "fire" end
    return "drain"
end
local function bump(t,key) t[key]=(t[key]or 0)+1 end
local function key(typ,var,sub)return typ..":"..var..":"..sub end
local function count(group,k) bump(total[group],k);bump(window[group],k) end
local function safe(fn) local ok,value=pcall(fn);if ok then return value end;return nil end
local function observeTargetLifecycle(event,e)
    if not cfg.observeAim or not active or done or type(targetVariant)~="number"
        or e.Type~=EntityType.ENTITY_EFFECT or e.Variant~=targetVariant then return end
    if targetTraceCount>=128 then targetTraceSuppressed=targetTraceSuppressed+1;return end
    targetTraceCount=targetTraceCount+1
    local dontOverwrite=type(EntityFlag)=="table" and EntityFlag.FLAG_DONT_OVERWRITE or nil
    local protected=nil
    if dontOverwrite~=nil then protected=safe(function()return e:HasEntityFlags(dontOverwrite)end) end
    emit("target_lifecycle",{event=event,index=e.Index,ptr_hash=GetPtrHash(e),init_seed=e.InitSeed,
        frame=e.FrameCount,position={e.Position.X,e.Position.Y},velocity={e.Velocity.X,e.Velocity.Y},
        spawner_type=e.SpawnerType,spawner_variant=e.SpawnerVariant,
        dont_overwrite_available=dontOverwrite~=nil,dont_overwrite_flag_type=type(dontOverwrite),
        dont_overwrite=protected,dont_overwrite_read_ok=type(protected)=="boolean",
        last_update_gf=lastUpdateGF,last_update_ui=lastUpdateUI,last_main_gf=lastMainGF,last_main_ui=lastMainUI,
        aim=safe(function()local v=Isaac.GetPlayer(0):GetAimDirection();return{v.X,v.Y}end),
        shot_births_total=shotBirths,input_queries=inputQueries,read_only=true})
end
local function players()
    local rows={};local itemConfig=Isaac.GetItemConfig();local limit=itemConfig:GetCollectibles().Size
    for i=0,game:GetNumPlayers()-1 do
        local p=Isaac.GetPlayer(i);local items={};local effects={}
        for id=1,limit-1 do
            local n=p:GetCollectibleNum(id);if n>0 then items[#items+1]={id=id,count=n} end
            local nfx=safe(function()return p:GetEffects():GetCollectibleEffectNum(id)end)
            if nfx and nfx>0 then effects[#effects+1]={id=id,count=nfx} end
        end
        local trinkets={}
        for slot=0,1 do local id=p:GetTrinket(slot);trinkets[#trinkets+1]={slot=slot,id=id,multiplier=id>0 and p:GetTrinketMultiplier(id)or 0} end
        rows[#rows+1]={index=i,type=p:GetPlayerType(),controller=p.ControllerIndex,init_seed=p.InitSeed,
            position={p.Position.X,p.Position.Y},velocity={p.Velocity.X,p.Velocity.Y},damage=p.Damage,
            fire_delay=p.FireDelay,max_fire_delay=p.MaxFireDelay,tear_range=p.TearRange,shot_speed=p.ShotSpeed,
            move_speed=p.MoveSpeed,luck=p.Luck,tear_flags=tostring(p.TearFlags),size=p.Size,
            sprite_scale={p.SpriteScale.X,p.SpriteScale.Y},hearts=p:GetHearts(),soul=p:GetSoulHearts(),bone=p:GetBoneHearts(),
            coins=p:GetNumCoins(),keys=p:GetNumKeys(),bombs=p:GetNumBombs(),active=p:GetActiveItem(),
            active_secondary=p:GetActiveItem(ActiveSlot.SLOT_SECONDARY),collectibles=items,collectible_effects=effects,trinkets=trinkets,
            marked=p:HasCollectible(CollectibleType.COLLECTIBLE_MARKED)}
    end
    return rows
end
local function aimContext(entities)
    local familiars,targets={},{}
    local familiarCount,targetCount=0,0
    for _,e in ipairs(entities or Isaac.GetRoomEntities()) do
        local isFamiliar=e.Type==EntityType.ENTITY_FAMILIAR
        local isTarget=type(targetVariant)=="number" and e.Type==EntityType.ENTITY_EFFECT and e.Variant==targetVariant
        if isFamiliar or isTarget then
            if isFamiliar then familiarCount=familiarCount+1 else targetCount=targetCount+1 end
            local list,limit=isFamiliar and familiars or targets,isFamiliar and 16 or 16
            if #list<limit then
                list[#list+1]={index=e.Index,type=e.Type,variant=e.Variant,subtype=e.SubType,init_seed=e.InitSeed,
                    frame=e.FrameCount,position={e.Position.X,e.Position.Y},velocity={e.Velocity.X,e.Velocity.Y},
                    spawner_type=e.SpawnerType,spawner_variant=e.SpawnerVariant,
                    parent_seed=safe(function()local parent=e.Parent;return parent and parent.InitSeed or nil end)}
                if isTarget then list[#list].dont_overwrite=safe(function()return e:HasEntityFlags(EntityFlag.FLAG_DONT_OVERWRITE)end) end
            end
        end
    end
    local function order(a,b) if a.init_seed==b.init_seed then return a.index<b.index end return a.init_seed<b.init_seed end
    table.sort(familiars,order);table.sort(targets,order)
    return {familiar_count=familiarCount,familiars=familiars,familiars_truncated=math.max(0,familiarCount-#familiars),
        target_enum_available=type(targetVariant)=="number",target_variant=targetVariant,target_count=targetCount,
        target_effects=targets,targets_truncated=math.max(0,targetCount-#targets),
        target_attribution="runtime EffectVariant.TARGET candidates; ownership is not assumed"}
end
local function observeAim(stage)
    if not cfg.observeAim or not active or done or game:GetFrameCount()-origin<cfg.settle+cfg.fire then return end
    local p=Isaac.GetPlayer(0)
    local aim=safe(function()local v=p:GetAimDirection();return{v.X,v.Y}end)
    local signature=aim and (tostring(math.floor(aim[1]*10000+0.5))..":"..tostring(math.floor(aim[2]*10000+0.5))) or "unavailable"
    local delay=p.FireDelay
    local reset=stage=="update" and lastUpdateFireDelay~=nil and delay>lastUpdateFireDelay+0.0001
    local changed=signature~=lastAimSignature
    local initial=lastAimSignature==nil
    local previousDelay=lastUpdateFireDelay
    if stage=="update" then lastUpdateFireDelay=delay end
    lastAimSignature=signature
    if not initial and not changed and not reset then return end
    if aimTraceCount>=64 then aimTraceSuppressed=aimTraceSuppressed+1;return end
    aimTraceCount=aimTraceCount+1
    emit("aim_transition",{stage=stage,initial_release_observation=initial,aim_changed=changed,fire_delay_reset=reset,
        aim=aim,fire_delay=delay,previous_update_fire_delay=previousDelay,player_position={p.Position.X,p.Position.Y},
        player_velocity={p.Velocity.X,p.Velocity.Y},shot_births_total=shotBirths,births_by_spawner=debugShotParents,
        input_queries=inputQueries,context=aimContext(),read_only=true})
end
local function census()
    local alive={},{}
    local types,variants,ages={}, {},{age0=0,age1=0,age2to5=0,age6to15=0,age16plus=0}
    local effects,unseen,countAll=0,0,0
    local samples={}
    local roomEntities=Isaac.GetRoomEntities()
    for _,e in ipairs(roomEntities) do
        countAll=countAll+1;bump(types,tostring(e.Type));bump(variants,key(e.Type,e.Variant,e.SubType))
        if e.Type==EntityType.ENTITY_EFFECT then
            effects=effects+1;observeEffectVariant(e.Variant)
            local identity=tostring(GetPtrHash(e))..":"..tostring(e.InitSeed)
            alive[identity]=true;if not knownEffects[identity]then unseen=unseen+1 end
            local age=e.FrameCount
            bump(ages,age==0 and"age0"or age==1 and"age1"or age<=5 and"age2to5"or age<=15 and"age6to15"or"age16plus")
            if #samples<8 then samples[#samples+1]={variant=e.Variant,subtype=e.SubType,age=age,spawner_type=e.SpawnerType,spawner_variant=e.SpawnerVariant} end
        end
    end
    local disappeared=0;for id in pairs(knownEffects)do if not alive[id]then disappeared=disappeared+1 end end
    knownEffects=alive
    local p=Isaac.GetPlayer(0);local aim=safe(function()local v=p:GetAimDirection();return{v.X,v.Y}end)
    local stick=safe(function()local v=p:GetShootingJoystick();return{v.X,v.Y}end)
    return {entities=countAll,effects=effects,types=types,variants=variants,effect_age_bins=ages,
        effect_identities_new_since_census=unseen,effect_identities_gone_since_census=disappeared,
        effect_samples=samples,aim=aim,shooting_joystick=stick,fire_delay=p.FireDelay,player_position={p.Position.X,p.Position.Y},
        aim_observation=cfg.observeAim and aimContext(roomEntities) or nil,
        births_by_spawner=cfg.observeAim and debugShotParents or nil}
end
local function flush(force)
    local now=Isaac.GetTime();if not force and now-windowWall<1000 then return end
    emit("window",{phase=phase(),window_ms=now-windowWall,pre_spawn=window.pre,effect_init=window.init,
        entity_remove=window.remove,init_brackets=window.brackets,init_samples=window.samples,
        census=census(),input_queries=inputQueries,shot_births_total=shotBirths,
        effect_variant_names=observedEffectNames})
    window={pre={},init={},remove={},brackets={},samples={}};windowWall=now
end
local function finish(status)
    if done then return end
    flush(true);emit("done",{status=status,phase=phase(),total=total,shot_births=shotBirths,input_queries=inputQueries,
        aim_trace_records=cfg.observeAim and aimTraceCount or nil,aim_trace_suppressed=cfg.observeAim and aimTraceSuppressed or nil,
        target_trace_records=cfg.observeAim and targetTraceCount or nil,target_trace_suppressed=cfg.observeAim and targetTraceSuppressed or nil,
        births_by_spawner=cfg.observeAim and debugShotParents or nil})
    active=false;done=true
    Isaac.DebugString("[HFTEST] DONE user-baseline-stress "..status)
end
probe:AddCallback(ModCallbacks.MC_POST_GAME_STARTED,function(_,continued)
    if active then emit("additional_start",{continued=continued});return end
    startWall=Isaac.GetTime();origin=game:GetFrameCount();windowWall=startWall
    if not continued then emit("rejected",{reason="requires_continue_no_new_run"});done=true;Isaac.DebugString("[HFTEST] DONE user-baseline-stress rejected");return end
    startSeed=game:GetSeeds():GetStartSeed();roomIndex=game:GetLevel():GetCurrentRoomIndex()
    if cfg.expectedSeed and startSeed~=Seeds.String2Seed(cfg.expectedSeed)then
        emit("rejected",{reason="unexpected_saved_run_seed",actual=startSeed});done=true;Isaac.DebugString("[HFTEST] DONE user-baseline-stress rejected");return
    end
    active=true;done=false
    emit("start",{continued=continued,seed=startSeed,room=roomIndex,stage=game:GetLevel():GetStage(),
        difficulty=game.Difficulty,config=cfg,players=players(),time_counter=game.TimeCounter,
        phase_bracket_limit="Lua markers describe callback ordering, not authoritative native hidden/pure render attribution."})
    Isaac.DebugString("[HFTEST] started user-baseline-stress")
end)
probe:AddCallback(ModCallbacks.MC_INPUT_ACTION,function(_,entity,hook,action)
    if not active or done or not entity or not entity:ToPlayer()then return end
    if entity.Index~=Isaac.GetPlayer(0).Index then return end
    if action<ButtonAction.ACTION_LEFT or action>ButtonAction.ACTION_SHOOTDOWN then return end
    inputQueries=inputQueries+1
    local shooting=phase()=="fire" and action==ButtonAction.ACTION_SHOOTRIGHT
    if hook==InputHook.GET_ACTION_VALUE then return shooting and 1.0 or 0.0 end
    if hook==InputHook.IS_ACTION_PRESSED then return shooting end
    if hook==InputHook.IS_ACTION_TRIGGERED then return shooting and game:GetFrameCount()-origin==cfg.settle end
end)
probe:AddCallback(ModCallbacks.MC_PRE_ENTITY_SPAWN,function(_,typ,var,sub,pos,vel,spawner,seed)
    if not active or done then return end
    if typ==2 or typ==7 or typ==8 or typ==1000 then
        count("pre",key(typ,var,sub))
        if typ==2 or typ==7 or typ==8 then
            shotBirths=shotBirths+1
            if cfg.observeAim then
                local parentType,parentVariant=spawner and spawner.Type or -1,spawner and spawner.Variant or -1
                bump(debugShotParents,key(typ,var,sub).."<-"..parentType..":"..parentVariant)
            end
        end
    end
    -- nil preserves the original native entity specification.
end)
probe:AddCallback(ModCallbacks.MC_POST_EFFECT_INIT,function(_,e)
    if not active or done then return end
    count("init",key(e.Type,e.Variant,e.SubType));observeEffectVariant(e.Variant)
    observeTargetLifecycle("init",e)
    local gf,ui=game:GetFrameCount(),Isaac.GetFrameCount()
    local bracket=(gf==lastMainGF and ui==lastMainUI)and"same_tick_after_main"or
        (gf==lastUpdateGF and ui==lastUpdateUI)and"same_tick_after_update"or"other_native_interval"
    bump(window.brackets,bracket)
    if #window.samples<8 then window.samples[#window.samples+1]={gf=gf,ui=ui,variant=e.Variant,subtype=e.SubType,
        spawner_type=e.SpawnerType,spawner_variant=e.SpawnerVariant,age=e.FrameCount,bracket=bracket,last_update_gf=lastUpdateGF,last_update_ui=lastUpdateUI,last_main_gf=lastMainGF,last_main_ui=lastMainUI} end
end)
probe:AddCallback(ModCallbacks.MC_POST_ENTITY_REMOVE,function(_,e)
    if active and not done and(e.Type==2 or e.Type==7 or e.Type==8 or e.Type==1000)then
        count("remove",key(e.Type,e.Variant,e.SubType))
        observeTargetLifecycle("remove",e)
    end
end)
probe:AddCallback(ModCallbacks.MC_POST_UPDATE,function()
    if not active or done then return end
    updates=updates+1;lastUpdateGF=game:GetFrameCount();lastUpdateUI=Isaac.GetFrameCount()
    if updates==1 then
        if game:GetNumPlayers()~=1 then finish("normalization_requires_single_player");return end
        local p=Isaac.GetPlayer(0)
        local before={p.Position.X,p.Position.Y};local beforeVelocity={p.Velocity.X,p.Velocity.Y}
        p.Position=Vector(cfg.normalizeFirstUpdatePosition[1],cfg.normalizeFirstUpdatePosition[2])
        p.Velocity=Vector.Zero
        local appliedExtra={requested=false}
        local requested=cfg.extraCollectibles or (cfg.extraCollectible and {cfg.extraCollectible}) or {}
        if type(requested)~="table" or #requested>3 then finish("invalid_extra_collectible_list");return end
        local seen,added={},{}
        for _,id in ipairs(requested) do
            if seen[id] then finish("duplicate_extra_collectible");return end
            if type(id)~="number" or id~=math.floor(id) or id<=0 or id>=Isaac.GetItemConfig():GetCollectibles().Size or not Isaac.GetItemConfig():GetCollectible(id) then
                emit("extra_rejected",{reason="invalid_collectible_id",requested=tostring(id)})
                finish("invalid_extra_collectible");return
            end
            seen[id]=true
            local countBefore=p:GetCollectibleNum(id)
            p:AddCollectible(id,0,true)
            local countAfter=p:GetCollectibleNum(id)
            appliedExtra={requested=true,id=id,before_count=countBefore,after_count=countAfter,delta=countAfter-countBefore}
            if countAfter~=countBefore+1 then
                emit("extra_rejected",{reason="expected_exactly_one_addition",applied_extra=appliedExtra})
                finish("extra_collectible_count_mismatch");return
            end
            added[#added+1]=appliedExtra
        end
        if #added>1 then appliedExtra={requested=true,items=added,count=#added} end
        emit("ready",{normalized_once=true,original_position=before,original_velocity=beforeVelocity,
            players=players(),census=census(),time_counter=game.TimeCounter,applied_extra=appliedExtra,
            normalization_scope="single player position and velocity once; optional explicitly requested collectible additions"})
    end
    if game:GetLevel():GetCurrentRoomIndex()~=roomIndex or game:GetSeeds():GetStartSeed()~=startSeed then finish("room_or_run_changed");return end
    observeAim("update")
    local name=phase()
    if name~=lastPhase then flush(true);emit("phase",{phase=name,time_counter=game.TimeCounter});lastPhase=name end
    flush(false)
    if game:GetFrameCount()-origin>=cfg.settle+cfg.fire+cfg.drain then finish(shotBirths>0 and"complete"or"no_natural_projectiles_check_marked_input")end
end)
probe:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    if not active or done then return end
    renders=renders+1;lastMainGF=game:GetFrameCount();lastMainUI=Isaac.GetFrameCount()
    observeAim("main_render")
    if Isaac.GetTime()-startWall>cfg.maxWallMs then finish("wall_timeout")end
end)
