-- Shared read-only helpers. Copy alongside chosen fixture as ce_compat_probe.lua.
local P = {}
local json = require("json")
function P.log(tag, data)
    data.wall_ms = Isaac.GetTime()
    data.difficulty = Game().Difficulty
    Isaac.DebugString("["..tag.."] "..json.encode(data))
end
function P.requireNormal()
    assert(Game().Difficulty==0,"Fixture requires Normal difficulty 0; run generic harness with --normal")
end
function P.queueSeededCharacter(playerType, seed)
    -- J374 restart <char> sets Manager+4B004; seed alone retains that field.
    -- Both commands queue Manager+4B000 in a single-player POST_UPDATE. The
    -- second command supplies the final Seeds snapshot before the queued start.
    -- Never replace this with ChangePlayerType: that does not select next run.
    P.requireNormal()
    assert(Game():GetNumPlayers()==1,"Seeded character queue is single-player only")
    assert(type(playerType)=="number" and playerType>=0 and playerType%1==0,"Invalid character enum")
    assert(Seeds.IsStringValidSeed(seed),"Invalid fixture seed")
    P.log("HF_START_QUEUE",{phase="before",character=playerType,seed=seed,gf=Game():GetFrameCount()})
    Isaac.ExecuteCommand("restart "..tostring(playerType))
    Isaac.ExecuteCommand("seed "..seed)
    P.log("HF_START_QUEUE",{phase="queued",character=playerType,seed=seed,gf=Game():GetFrameCount()})
end
function P.menu()
    return ModConfigMenu or MCM
end
function P.settings()
    local m=P.menu()
    assert(m and m.MenuData,"MCM required to observe actual CE settings")
    local found={}
    for _,cat in ipairs(m.MenuData) do
        if cat.Name=="Character Enhance" then
            for _,sub in ipairs(cat.Subcategories or {}) do
                for _,opt in ipairs(sub.Options or {}) do
                    if type(opt.CurrentSetting)=="function" then
                        local display=type(opt.Display)=="function" and opt.Display() or opt.Display
                        found[#found+1]={group=sub.Name,label=tostring(display),value=opt.CurrentSetting()}
                    end
                end
            end
        end
    end
    assert(#found>0,"CE MCM category not registered; no feature coverage")
    return found
end
function P.requireEnabled(rows, en, zh)
    for _,row in ipairs(rows) do
        if row.label:sub(1,#en)==en or row.label:sub(1,#zh)==zh then
            assert(row.value==true,"Required feature disabled: "..row.label)
            return
        end
    end
    error("Required feature not observed: "..en)
end
function P.ceSave()
    -- Read the installed module's already-persisted state; never call SaveData.
    assert(Isaac.GetCallbacks,"Need J374 GetCallbacks to inspect CE save owner")
    for _,cb in ipairs(Isaac.GetCallbacks(ModCallbacks.MC_POST_GAME_STARTED)) do
        if cb.Mod and cb.Mod.Name=="character-enhance" then
            if not cb.Mod:HasData() then return {} end
            return json.decode(cb.Mod:LoadData())
        end
    end
    error("CE callback owner not found; do not infer pending reward count")
end
function P.pendingRewards()
    local s=P.ceSave()
    local d=s.modules and s.modules.edenBlessingDuplicateFix
    return d and d.pendingRewards or 0
end
function P.player(p)
    local s=p:GetSprite()
    return {type=p:GetPlayerType(),seed=p.InitSeed,ptr=GetPtrHash(p),x=p.Position.X,y=p.Position.Y,
        vx=p.Velocity.X,vy=p.Velocity.Y,scale_x=p.SpriteScale.X,scale_y=p.SpriteScale.Y,
        hearts=p:GetHearts(),soul=p:GetSoulHearts(),charge=p:GetSoulCharge(),cooldown=p:GetDamageCooldown(),
        controls=p.ControlsEnabled,animation=s:GetAnimation(),animation_frame=s:GetFrame(),
        active=p:GetActiveItem(),active_charge=p:GetActiveCharge(),coins=p:GetNumCoins(),
        bombs=p:GetNumBombs(),keys=p:GetNumKeys()}
end
function P.pickups()
    local rows={}
    for _,e in ipairs(Isaac.FindByType(EntityType.ENTITY_PICKUP,PickupVariant.PICKUP_COLLECTIBLE,-1,false,false)) do
        local p=e:ToPickup();local data=p:GetData()
        rows[#rows+1]={id=p.SubType,seed=p.InitSeed,options=p.OptionsPickupIndex,
            x=p.Position.X,y=p.Position.Y,touched=p.Touched,animation=p:GetSprite():GetAnimation(),
            ce_kind=data.CharacterEnhanceEdenChoiceKind,ce_pool=data.CharacterEnhanceEdenChoicePool}
    end
    table.sort(rows,function(a,b) if a.options~=b.options then return a.options<b.options end return a.seed<b.seed end)
    return rows
end
function P.inventory(p)
    local result={}
    local count=Isaac.GetItemConfig():GetCollectibles().Size
    for id=1,count-1 do
        local n=p:GetCollectibleNum(id,true)
        if n>0 then result[#result+1]={id=id,count=n} end
    end
    return result
end
return P
