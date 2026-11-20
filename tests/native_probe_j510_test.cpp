#include "../src/native_probe_j510.h"
#include <cstdio>

using namespace highfps::j510;

static_assert(Matches(0x6AFE6324, 0x953000));
static_assert(!Matches(0x6AFE6324, 0x8B6000));
static_assert(!Matches(0x68D45723, 0x953000));
static_assert(!Matches(0x68D45723, 0x8B6000));
static_assert(!CanMeasure(true, true));
static_assert(!CanMeasure(true, false));
static_assert(!CanMeasure(false, false));
static_assert(CanMeasure(false, true));

int main() {
    std::puts("J510 header/config probe gate: passed");
}
