#include <cassert>
#include <cstdio>
#include "lua_work_control.h"
using namespace highfps::work_control;
int main(){
    for(unsigned mask=0;mask<8;++mask) {
        const auto s=FailOpenSettings({true,true,true,true},(mask&1)!=0,(mask&2)!=0,(mask&4)!=0);
        assert(s.spawn==((mask&3)==3)&&s.extra==s.spawn&&s.poof==((mask&4)!=0)&&s.adaptive);
    }
    const auto mixed=FailOpenSettings({true,false,false,true},true,true,true);
    assert(mixed.spawn&&!mixed.extra&&!mixed.poof&&mixed.adaptive);
    Controller c;Settings result{true,true,true,true};
    assert(!c.Available(Key::Attack));assert(!c.Request(Key::Attack,1));assert(!c.Apply(true,result));
    c.Initialize({true,false,false,true},true,true,true);
    assert(c.Desired(Key::Attack)==1);assert(c.Desired(Key::Adaptive)==1);
    assert(c.Request(Key::Attack,1));assert(c.Desired(Key::Attack)==1);
    assert(c.Desired(Key::AttackDefault)==1&&c.Desired(Key::AdaptiveDefault)==1);
    assert(!c.Request(Key::AttackDefault,1));
    assert(!c.Apply(false,result));assert(c.Apply(true,result));assert(result.spawn&&result.extra);
    assert(!c.Apply(true,result)); // No-op changes cannot refill an active quota.
    assert(c.Request(Key::Attack,-1));assert(c.Apply(true,result)&&result.spawn&&!result.extra);
    assert(c.Desired(Key::Attack)==1);
    assert(c.Request(Key::Attack,0));assert(c.Apply(true,result)&&!result.spawn&&!result.extra);
    assert(c.Request(Key::Attack,-1));assert(c.Apply(true,result)&&result.spawn&&!result.extra);
    assert(c.Request(Key::Attack,1));assert(c.Apply(true,result)&&result.spawn&&result.extra);
    assert(c.Request(Key::Attack,1));assert(!c.Apply(true,result));
    assert(c.Request(Key::Poof,1));assert(c.Request(Key::Adaptive,0));
    assert(c.Apply(true,result)&&result.poof&&!result.adaptive);
    assert(c.Desired(Key::AdaptiveDefault)==1&&c.Desired(Key::PoofDefault)==0);
    assert(c.Request(Key::Poof,0));assert(c.Request(Key::Poof,1));assert(!c.Apply(true,result));
    assert(!c.Request(Key::Invalid,1));assert(!c.Request(Key::Attack,2));assert(!c.Request(Key::Attack,-2));assert(!c.Apply(true,result));
    c.Initialize({},false,true,false);
    assert(!c.Request(Key::Attack,1));assert(!c.Available(Key::Adaptive));assert(!c.Available(Key::AttackDefault));assert(c.Request(Key::Poof,1));
    assert(c.Apply(true,result)&&result.poof&&!result.spawn&&!result.extra);
    assert(Parse("attackBudgetDefault",19)==Key::AttackDefault);
    assert(Parse("poofBudgetDefault",17)==Key::PoofDefault);
    assert(Parse("adaptiveBudgetDefault",21)==Key::AdaptiveDefault);
    assert(Parse("attackBudget",12)==Key::Attack);assert(Parse("poofBudget",10)==Key::Poof);
    assert(Parse("adaptiveBudget",14)==Key::Adaptive);assert(Parse("attackBudget\0junk",17)==Key::Invalid);
    assert(Parse("attackBudget",11)==Key::Invalid);assert(Parse(nullptr,12)==Key::Invalid);
    std::puts("Lua work control queue tests passed");
}
