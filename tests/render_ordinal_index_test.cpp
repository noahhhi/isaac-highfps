#include "render_ordinal_index.h"
#include <cassert>
#include <cstdio>
int main() {
    highfps::RenderOrdinalIndex index;
    std::uint32_t n=99;
    for(unsigned frame=0;frame<5;++frame) {
        index.Clear();
        for(unsigned occurrence=0;occurrence<3;++occurrence)
            for(unsigned i=0;i<10000;++i) {
                assert(index.Next(0x10000+i*16,41,i%3,n));assert(n==occurrence);
            }
        assert(index.Size()==10000);
        assert(index.Next(0x10000,56,0,n)&&n==0); // callback identity
        assert(index.Next(0x10000,41,99,n)&&n==0); // pass identity
    }
    index.Clear();
    for(std::size_t i=0;i<index.kCapacity;++i)assert(index.Next(i,56,0,n)&&n==0);
    n=123;assert(!index.Next(index.kCapacity,56,0,n)&&n==123);
    assert(index.Next(12,56,0,n)&&n==1); // full table still serves existing keys
    index.Clear();assert(index.Size()==0);
    assert(index.Next(index.kCapacity,56,0,n)&&n==0);
    std::puts("render ordinal identity, >8192 keys, frame reset and saturation: passed");
}
