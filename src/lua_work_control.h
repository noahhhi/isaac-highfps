#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// No VM, engine object, or Lua reference escapes a dispatch. The queue is owned
// by the game thread; only Apply(), called outside native/Lua scopes, commits it.
namespace highfps::work_control {
enum class Key { Attack, Poof, Adaptive, AttackDefault, PoofDefault, AdaptiveDefault, Invalid };
inline Key Base(Key key) {
    return key==Key::AttackDefault?Key::Attack:key==Key::PoofDefault?Key::Poof:key==Key::AdaptiveDefault?Key::Adaptive:key;
}
inline bool ReadOnly(Key key){return key!=Base(key);}
inline Key Parse(const char* name,std::size_t size) {
    if(!name)return Key::Invalid;
    if(size==19&&std::memcmp(name,"attackBudgetDefault",19)==0)return Key::AttackDefault;
    if(size==17&&std::memcmp(name,"poofBudgetDefault",17)==0)return Key::PoofDefault;
    if(size==21&&std::memcmp(name,"adaptiveBudgetDefault",21)==0)return Key::AdaptiveDefault;
    if(size==12&&std::memcmp(name,"attackBudget",12)==0)return Key::Attack;
    if(size==10&&std::memcmp(name,"poofBudget",10)==0)return Key::Poof;
    if(size==14&&std::memcmp(name,"adaptiveBudget",14)==0)return Key::Adaptive;
    return Key::Invalid;
}
struct Settings {
    bool spawn=false,extra=false,poof=false,adaptive=false;
    bool Same(const Settings&o)const{return spawn==o.spawn&&extra==o.extra&&poof==o.poof&&adaptive==o.adaptive;}
    int Get(Key key)const{return key==Key::Attack?(spawn||extra):key==Key::Poof?poof:adaptive;}
};
inline Settings FailOpenSettings(Settings settings,bool spawnReady,bool extraReady,bool poofReady) {
    if(!spawnReady||!extraReady)settings.spawn=settings.extra=false;
    if(!poofReady)settings.poof=false;
    return settings;
}
class Controller {
    Settings desired_{},effective_{},defaults_{};
    bool ready_=false,attack_=false,poof_=false,adaptive_=false;
public:
    void Initialize(Settings settings,bool attack,bool poof,bool adaptive) {
        desired_=effective_=defaults_=settings;attack_=attack;poof_=poof;adaptive_=adaptive;ready_=true;
    }
    bool Available(Key key)const {
        key=Base(key);
        return ready_&&(key==Key::Attack?attack_:key==Key::Poof?poof_:key==Key::Adaptive?adaptive_:false);
    }
    int Desired(Key key)const{return (ReadOnly(key)?defaults_:desired_).Get(Base(key));}
    bool Request(Key key,int value) {
        if(ReadOnly(key)||!Available(key)||value < -1||value > 1)return false;
        if(value==-1) {
            if(key==Key::Attack){desired_.spawn=defaults_.spawn;desired_.extra=defaults_.extra;}
            else if(key==Key::Poof)desired_.poof=defaults_.poof;
            else desired_.adaptive=defaults_.adaptive;
            return true;
        }
        if(key==Key::Attack)desired_.spawn=desired_.extra=value!=0;
        else if(key==Key::Poof)desired_.poof=value!=0;
        else desired_.adaptive=value!=0;
        return true;
    }
    bool Apply(bool safe,Settings&out) {
        if(!safe||!ready_||desired_.Same(effective_))return false;
        effective_=desired_;out=effective_;return true;
    }
};
#ifdef _WIN32
#define HF_CONTROL_CALL __cdecl
#else
#define HF_CONTROL_CALL
#endif
using Function=int(HF_CONTROL_CALL*)(void*);
struct LuaApi {
    int(HF_CONTROL_CALL*gettop)(void*)=nullptr;
    int(HF_CONTROL_CALL*checkstack)(void*,int)=nullptr;
    void(HF_CONTROL_CALL*settop)(void*,int)=nullptr;
    int(HF_CONTROL_CALL*type)(void*,int)=nullptr;
    int(HF_CONTROL_CALL*isinteger)(void*,int)=nullptr;
    std::int64_t(HF_CONTROL_CALL*tointeger)(void*,int,int*)=nullptr;
    const char*(HF_CONTROL_CALL*tostring)(void*,int,std::size_t*)=nullptr;
    void(HF_CONTROL_CALL*pushnil)(void*)=nullptr;
    void(HF_CONTROL_CALL*pushinteger)(void*,std::int64_t)=nullptr;
    const char*(HF_CONTROL_CALL*pushstring)(void*,const char*)=nullptr;
    void(HF_CONTROL_CALL*pushcclosure)(void*,Function,int)=nullptr;
    void(HF_CONTROL_CALL*setglobal)(void*,const char*)=nullptr;
    int(HF_CONTROL_CALL*rawgetp)(void*,int,const void*)=nullptr;
    void(HF_CONTROL_CALL*rawsetp)(void*,int,const void*)=nullptr;
    bool Complete()const{return gettop&&checkstack&&settop&&type&&isinteger&&tointeger&&tostring&&pushnil&&pushinteger&&pushstring&&pushcclosure&&setglobal&&rawgetp&&rawsetp;}
};
class Bridge {
    LuaApi lua_{};Controller*controller_=nullptr;bool(*owner_)()=nullptr;
    // A process-lifetime bridge owns this address. The marker lives in the VM;
    // closed/reused state addresses never reuse native pointer-keyed VM state.
    char marker_=0,rateMarker_=0;
    int Error(void*L,const char*message) {lua_.pushnil(L);lua_.pushstring(L,message);return 2;}
public:
    void Configure(LuaApi api,Controller*controller,bool(*owner)()) {lua_=api;controller_=controller;owner_=owner;}
    bool Ready()const{return lua_.Complete()&&controller_&&owner_;}
    // Returns true only for a newly observed VM. Presence publication shares this
    // marker so HIGH_FPS_NATIVE/RATE also survive VM allocator address reuse.
    bool Publish(void*L,Function entry) {
        if(!L||!Ready()||!owner_()||!lua_.checkstack(L,4))return false;
        constexpr int registry=-1001000;const int top=lua_.gettop(L);
        const int kind=lua_.rawgetp(L,registry,&marker_);lua_.settop(L,top);
        if(kind!=0)return false;
        lua_.pushcclosure(L,entry,0);lua_.setglobal(L,"HIGH_FPS_CONTROL");
        lua_.pushinteger(L,1);lua_.setglobal(L,"HIGH_FPS_CONTROL_VERSION");
        lua_.pushinteger(L,1);lua_.rawsetp(L,registry,&marker_);
        return true;
    }
    void PublishPresence(void*L,Function entry,std::int64_t nativeVersion,const char*version,std::int64_t rate) {
        if(!L||!Ready()||!owner_()||!lua_.checkstack(L,4))return;
        const bool fresh=Publish(L,entry);
        if(fresh) {
            lua_.pushinteger(L,nativeVersion);lua_.setglobal(L,"HIGH_FPS_NATIVE");
            lua_.pushstring(L,version);lua_.setglobal(L,"HIGH_FPS_VERSION");
        }
        constexpr int registry=-1001000;const int top=lua_.gettop(L);
        const int kind=lua_.rawgetp(L,registry,&rateMarker_);
        const bool update=kind!=3||lua_.tointeger(L,-1,nullptr)!=rate;
        lua_.settop(L,top);
        if(update) {
            lua_.pushinteger(L,rate);lua_.setglobal(L,"HIGH_FPS_RATE");
            lua_.pushinteger(L,rate);lua_.rawsetp(L,registry,&rateMarker_);
        }
    }
    int Invoke(void*L) {
        if(!owner_())return Error(L,"HIGH_FPS_CONTROL requires the game thread");
        const int count=lua_.gettop(L);
        if((count!=1&&count!=2)||lua_.type(L,1)!=4)return Error(L,"expected key and optional integer -1, 0 or 1");
        std::size_t length=0;const char*name=lua_.tostring(L,1,&length);const Key key=Parse(name,length);
        if(key==Key::Invalid)return Error(L,"unknown HIGH_FPS_CONTROL key");
        std::int64_t value=0;
        if(count==2) {
            if(ReadOnly(key))return Error(L,"startup default keys are read-only");
            if(!lua_.isinteger(L,2))return Error(L,"value must be integer -1, 0 or 1");
            value=lua_.tointeger(L,2,nullptr);
            if(value < -1||value > 1)return Error(L,"value must be integer -1, 0 or 1");
        }
        if(!controller_->Available(key))return Error(L,"native work control is unavailable or not ready");
        if(count==2)controller_->Request(key,int(value));
        lua_.pushinteger(L,controller_->Desired(key));return 1;
    }
};
#undef HF_CONTROL_CALL
}
