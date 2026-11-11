// External actual x86 Lua 5.3 DLL; no engine process, game state or save access.
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "lua_work_control.h"
using namespace highfps::work_control;
static Bridge bridge;static Controller control;static bool owner=true;
static bool Owner(){return owner;}
static int __cdecl Entry(void*L){return bridge.Invoke(L);}
int main(int argc,char**argv){
    if(argc!=2)return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);
    auto symbol=[&](const char*name){FARPROC p=GetProcAddress(module,name);if(!p){std::fprintf(stderr,"missing %s\n",name);std::exit(2);}return p;};
    LuaApi api;
#define LOAD(member,name) api.member=reinterpret_cast<decltype(api.member)>(symbol(name))
    LOAD(gettop,"lua_gettop");LOAD(checkstack,"lua_checkstack");LOAD(settop,"lua_settop");LOAD(type,"lua_type");
    LOAD(isinteger,"lua_isinteger");LOAD(tointeger,"lua_tointegerx");LOAD(tostring,"lua_tolstring");LOAD(pushnil,"lua_pushnil");
    LOAD(pushinteger,"lua_pushinteger");LOAD(pushstring,"lua_pushstring");LOAD(pushcclosure,"lua_pushcclosure");
    LOAD(setglobal,"lua_setglobal");LOAD(rawgetp,"lua_rawgetp");LOAD(rawsetp,"lua_rawsetp");
#undef LOAD
    auto newState=reinterpret_cast<void*(__cdecl*)()>(symbol("luaL_newstate"));
    auto close=reinterpret_cast<void(__cdecl*)(void*)>(symbol("lua_close"));
    auto open=reinterpret_cast<void(__cdecl*)(void*)>(symbol("luaL_openlibs"));
    auto load=reinterpret_cast<int(__cdecl*)(void*,const char*)>(symbol("luaL_loadstring"));
    auto call=reinterpret_cast<int(__cdecl*)(void*,int,int,int,std::intptr_t,void*)>(symbol("lua_pcallk"));
    bridge.Configure(api,&control,&Owner);assert(bridge.Ready());
    auto run=[&](void*L,const char*script){int r=load(L,script);if(!r)r=call(L,0,0,0,0,nullptr);if(r){std::fprintf(stderr,"%s\n",api.tostring(L,-1,nullptr));std::exit(1);}};
    void*L=newState();assert(L);open(L);
    // Publication must preserve an existing engine callback's arguments.
    api.pushinteger(L,0x123456789LL);api.pushstring(L,"callback-arg");
    bridge.PublishPresence(L,&Entry,2,"test-version",90);
    assert(api.gettop(L)==2&&api.tointeger(L,1,nullptr)==0x123456789LL);
    assert(std::strcmp(api.tostring(L,2,nullptr),"callback-arg")==0);api.settop(L,0);
    run(L,"assert(HIGH_FPS_CONTROL_VERSION==1 and HIGH_FPS_NATIVE==2 and HIGH_FPS_RATE==90 and HIGH_FPS_VERSION=='test-version'); local v,e=HIGH_FPS_CONTROL('attackBudget'); assert(v==nil and type(e)=='string')");
    control.Initialize({},true,true,true);
    run(L,R"(
        assert(HIGH_FPS_CONTROL('attackBudget')==0)
        assert(HIGH_FPS_CONTROL('attackBudget',1)==1)
        assert(HIGH_FPS_CONTROL('attackBudget')==1)
        assert(HIGH_FPS_CONTROL('attackBudgetDefault')==0)
        local function bad(...) local v,e=HIGH_FPS_CONTROL(...);assert(v==nil and type(e)=='string') end
        bad('attackBudgetDefault',1);bad('poofBudgetDefault',0);bad('adaptiveBudgetDefault',1)
        bad();bad(1);bad({});bad('unknown');bad('attackBudget\0junk');bad('attackBudget',true)
        bad('attackBudget','1');bad('attackBudget',1.0);bad('attackBudget',1.5);bad('attackBudget',2)
        bad('attackBudget',0x100000000);bad('attackBudget',-2);bad('attackBudget',nil);bad('attackBudget',0,0)
        assert(HIGH_FPS_CONTROL('attackBudget')==1)
        assert(HIGH_FPS_CONTROL('attackBudgetDefault')==0)
    )");
    Settings next;assert(!control.Apply(false,next));assert(control.Apply(true,next)&&next.spawn&&next.extra);assert(!control.Apply(true,next));
    owner=false;run(L,"local v,e=HIGH_FPS_CONTROL('attackBudget',0);assert(v==nil and type(e)=='string')");owner=true;
    run(L,"assert(HIGH_FPS_CONTROL('attackBudget')==1)");
    control.Initialize({true,false,false,true},true,true,true);
    run(L,"assert(HIGH_FPS_CONTROL('attackBudget')==1 and HIGH_FPS_CONTROL('attackBudgetDefault')==1);assert(HIGH_FPS_CONTROL('attackBudget',1)==1)");
    assert(control.Apply(true,next)&&next.spawn&&next.extra);
    run(L,"assert(HIGH_FPS_CONTROL('attackBudget',-1)==1)");
    assert(control.Apply(true,next)&&next.spawn&&!next.extra);
    run(L,"assert(HIGH_FPS_CONTROL('attackBudget',0)==0);assert(HIGH_FPS_CONTROL('adaptiveBudget',0)==0)");
    assert(control.Apply(true,next)&&!next.spawn&&!next.extra&&!next.adaptive);
    run(L,"assert(HIGH_FPS_CONTROL('attackBudget',-1)==1);assert(HIGH_FPS_CONTROL('adaptiveBudget',-1)==1)");
    assert(control.Apply(true,next)&&next.spawn&&!next.extra&&next.adaptive);
    control.Initialize({},false,true,false);
    run(L,"local v,e=HIGH_FPS_CONTROL('attackBudget',1);assert(v==nil and type(e)=='string');assert(HIGH_FPS_CONTROL('poofBudget',1)==1)");
    bridge.PublishPresence(L,&Entry,2,"test-version",80);run(L,"assert(HIGH_FPS_RATE==80)");
    // Independent live VMs must not share the last-published-rate cache.
    void*second=newState();open(second);bridge.PublishPresence(second,&Entry,2,"test-version",80);
    run(second,"assert(HIGH_FPS_NATIVE==2 and HIGH_FPS_RATE==80 and type(HIGH_FPS_CONTROL)=='function')");
    bridge.PublishPresence(L,&Entry,2,"test-version",90);bridge.PublishPresence(second,&Entry,2,"test-version",90);
    run(L,"assert(HIGH_FPS_RATE==90)");run(second,"assert(HIGH_FPS_RATE==90)");close(second);close(L);
    // Repeated close/new-state cycles exercise allocator reuse with no native VM
    // address cache. Publication must happen even when Lua reuses the address.
    for(int i=0;i<128;++i){L=newState();assert(L);open(L);bridge.PublishPresence(L,&Entry,2,"test-version",90);run(L,"assert(HIGH_FPS_CONTROL_VERSION==1 and HIGH_FPS_NATIVE==2 and HIGH_FPS_RATE==90 and type(HIGH_FPS_CONTROL)=='function');assert(HIGH_FPS_CONTROL('poofBudgetDefault')==0 and HIGH_FPS_CONTROL('poofBudget')==1)");close(L);}
    FreeLibrary(module);std::puts("actual Lua work control publication/ABI/lifecycle tests passed");
}
