#include "../src/j510_motion_projection.h"
#include <cassert>
using namespace highfps::j510_motion;
int main() {
    const auto halfway = At({2, 14}, {10, 6}, .5f);
    assert(halfway.x == 6 && halfway.y == 10);
    const auto native = At({2, 14}, {10, 6}, 2);
    assert(native.x == 10 && native.y == 6);
    const auto offset = AddProjection({4, -8}, {20, 12}, {12, 20});
    assert(offset.x == -4 && offset.y == 0);
    // The native input offset must not move when the two projections match.
    const auto same = AddProjection({4, -8}, {20, 12}, {20, 12});
    assert(same.x == 4 && same.y == -8);
}
