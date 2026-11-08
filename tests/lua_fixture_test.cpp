// Runs only the supplied offline Lua harness. No game process or save access.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using State=void;
int main(int argc,char** argv) {
    if(argc!=4) {std::fputs("usage: lua_fixture_test LUA_DLL OFFLINE_HARNESS FIXTURE\n",stderr);return 2;}
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    HMODULE module=LoadLibraryA(argv[1]);
    if(!module) {std::fprintf(stderr,"Lua runtime load failed: %lu\n",GetLastError());return 2;}
    auto symbol=[&](const char* name)->FARPROC {
        auto result=GetProcAddress(module,name);
        if(!result) {std::fprintf(stderr,"missing Lua API: %s\n",name);std::exit(2);}
        return result;
    };
    auto newState=reinterpret_cast<State*(__cdecl*)()>(symbol("luaL_newstate"));
    auto close=reinterpret_cast<void(__cdecl*)(State*)>(symbol("lua_close"));
    auto openLibs=reinterpret_cast<void(__cdecl*)(State*)>(symbol("luaL_openlibs"));
    auto createTable=reinterpret_cast<void(__cdecl*)(State*,int,int)>(symbol("lua_createtable"));
    auto pushString=reinterpret_cast<const char*(__cdecl*)(State*,const char*)>(symbol("lua_pushstring"));
    auto rawSetI=reinterpret_cast<void(__cdecl*)(State*,int,std::int64_t)>(symbol("lua_rawseti"));
    auto setGlobal=reinterpret_cast<void(__cdecl*)(State*,const char*)>(symbol("lua_setglobal"));
    auto loadFile=reinterpret_cast<int(__cdecl*)(State*,const char*,const char*)>(symbol("luaL_loadfilex"));
    auto call=reinterpret_cast<int(__cdecl*)(State*,int,int,int,std::intptr_t,void*)>(symbol("lua_pcallk"));
    auto text=reinterpret_cast<const char*(__cdecl*)(State*,int,std::size_t*)>(symbol("lua_tolstring"));
    State* state=newState();if(!state)return 2;openLibs(state);
    createTable(state,2,0);pushString(state,argv[3]);rawSetI(state,-2,1);setGlobal(state,"arg");
    int result=loadFile(state,argv[2],nullptr);
    if(!result)result=call(state,0,0,0,0,nullptr);
    if(result)std::fprintf(stderr,"offline fixture failure: %s\n",text(state,-1,nullptr));
    close(state);FreeLibrary(module);
    return result?1:0;
}
