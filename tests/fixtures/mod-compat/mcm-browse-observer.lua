-- Read-only observer for ROOT-driven isolated keyboard sequence.
-- No input generation, menu-opening call, setting write, render substitute or SaveData.
local json=require("json")
local P=include("ce_compat_probe")
local mod=RegisterMod("HighFPS isolated MCM browse observer",1)
local game=Game()
local renders,updates=0,0
local baseline,seenOpen,lastVisible,closedAt,lastFocus=nil,false,false,nil,nil
local function settings()
    local m=P.menu();local values={}
    for ci,cat in ipairs(m.MenuData or {}) do
        for si,sub in ipairs(cat.Subcategories or {}) do
            for oi,opt in ipairs(sub.Options or {}) do
                if opt.CurrentSetting~=nil then
                    local ok,v=true,opt.CurrentSetting
                    if type(v)=="function" then ok,v=pcall(v) end
                    if ok and (type(v)=="boolean" or type(v)=="number" or type(v)=="string") then
                        values[#values+1]={category=ci,subcategory=si,option=oi,value=v}
                    end
                end
            end
        end
    end
    return json.encode(values)
end
local function emit(kind)
    local m=P.menu();local focus=m.GetCurrentFocus();local p=Isaac.GetPlayer(0)
    P.log("HFMCM",{kind=kind,u=updates,r=renders,gf=game:GetFrameCount(),ui=Isaac.GetFrameCount(),
        visible=m.IsVisible,category=focus.category and focus.category.Name,
        subcategory=focus.subcategory and focus.subcategory.Name,
        option_type=focus.option and focus.option.Type,player=P.player(p)})
end
mod:AddCallback(ModCallbacks.MC_POST_UPDATE,function() updates=updates+1 end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER,function()
    renders=renders+1
    local m=P.menu()
    if not m or not m.GetCurrentFocus or game:GetNumPlayers()==0 then return end
    if not baseline and renders>=30 and not m.IsVisible then
        baseline=settings()
        local key=m.Config["Mod Config Menu"].OpenMenuKeyboard or Keyboard.KEY_L
        P.log("HFMCM_READY",{keyboard_key=key,default_L=key==Keyboard.KEY_L,
            initial_settings=json.decode(baseline),player=P.player(Isaac.GetPlayer(0))})
        Isaac.DebugString("[HFTEST] started mcm-browse")
    end
    if not baseline then return end
    local focus=m.GetCurrentFocus()
    local key=(focus.category and focus.category.Name or "").."/"..(focus.subcategory and focus.subcategory.Name or "")
    if m.IsVisible~=lastVisible or key~=lastFocus or renders%15==0 then emit("observe") end
    lastFocus=key
    if m.IsVisible and not lastVisible then
        seenOpen=true;emit("opened")
        assert(game:GetRoom():IsClear(),"MCM test must use a safe room")
    elseif not m.IsVisible and lastVisible then
        closedAt=renders;emit("closed")
    end
    lastVisible=m.IsVisible
    if seenOpen and closedAt and renders==closedAt+60 then
        local after=settings()
        P.log("HFMCM_FINAL",{settings_unchanged=after==baseline,final_settings=json.decode(after),
            visible=m.IsVisible,player=P.player(Isaac.GetPlayer(0))})
        assert(after==baseline,"Browsing changed a registered setting")
        assert(not m.IsVisible and Isaac.GetPlayer(0).ControlsEnabled,"MCM did not restore controls")
        Isaac.DebugString("[HFTEST] DONE mcm-browse")
    end
end)
