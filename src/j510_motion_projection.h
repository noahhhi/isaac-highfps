#pragma once

namespace highfps::j510_motion {
struct Position { float x, y; };
inline Position At(Position old, Position current, float alpha) {
    if (!(alpha >= 0)) alpha = 0;
    if (alpha > 1) alpha = 1;
    return {old.x + (current.x - old.x) * alpha,
            old.y + (current.y - old.y) * alpha};
}
inline Position AddProjection(Position nativeOffset, Position nativeScreen, Position visualScreen) {
    return {nativeOffset.x + (visualScreen.x - nativeScreen.x),
            nativeOffset.y + (visualScreen.y - nativeScreen.y)};
}
}
