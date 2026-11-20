#include <windows.h>
#include <intrin.h>
#include <cassert>
#include "../src/native_shader_locations_j510.h"

using namespace highfps::j510_locations;
namespace {
int result = 7, calls = 0, mutations = 0;
HGLRC fakeContext = reinterpret_cast<HGLRC>(0x1234);
HGLRC WINAPI Current() { return fakeContext; }
int WINAPI Driver(unsigned, const char*) { ++calls; return result; }
void WINAPI DriverMutation(unsigned) { ++mutations; }
void* fakeSlots[Count]{};
DWORD WINAPI ForeignMutation(void*) { Mutation<Link, void>(23u); return 0; }
}

int main() {
    cache = new Cache;
    owner = GetCurrentThreadId();
    current = &Current;
    for (unsigned i = 0; i < Count; ++i) {
        fakeSlots[i] = hook[i] = reinterpret_cast<void*>(&Driver);
        slots[i] = &fakeSlots[i];
        targets[i].store(hook[i]);
    }
    targets[Link].store(reinterpret_cast<void*>(&DriverMutation));
    targets[Delete].store(reinterpret_cast<void*>(&DriverMutation));
    callers[Attrib][0] = 1;
    callers[Uniform][0] = 2;
    healthy.store(true);
    auto attrib = [](unsigned p = 23) { return Query(Attrib, p, "Position", 1); };
    Begin();
    assert(attrib() == 7 && attrib() == 7 && calls == 1);
    assert(Query(Uniform, 23, "Position", 2) == 7 && calls == 2);
    assert(Query(Attrib, 23, "Position", 999) == 7 && calls == 3);
    result = 8;
    Mutation<Link, void>(23u);
    assert(mutations == 1 && attrib() == 8 && attrib() == 8 && calls == 5);
    End();
    Begin();
    assert(attrib() == 8 && attrib() == 8 && calls == 6);
    fakeContext = reinterpret_cast<HGLRC>(0x2345);
    result = 9;
    assert(attrib() == 9 && calls == 7);
    End();
    Begin();
    result = -1;
    assert(attrib() == -1 && attrib() == -1 && calls == 9);
    Mutation<Delete, void>(23u);
    assert(mutations == 2);
    End();
    Begin();
    result = 10;
    assert(attrib() == 10 && attrib() == 10 && calls == 11);
    assert(attrib(17) == 10 && attrib(17) == 10 && calls == 12);
    HANDLE t = CreateThread(nullptr, 0, ForeignMutation, nullptr, 0, nullptr);
    assert(t && WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    assert(!healthy.load() && attrib(17) == 10 && calls == 13);
    End();
    healthy.store(true);
    Begin();
    assert(attrib(17) == 10 && calls == 14);
    fakeSlots[Link] = nullptr; // Simulate loss of a lifetime hook between calls.
    result = 11;
    assert(attrib(17) == 11 && calls == 15 && !healthy.load());
    End();
}
