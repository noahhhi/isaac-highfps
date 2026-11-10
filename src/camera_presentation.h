#pragma once
#include <cmath>
namespace highfps { namespace camera_presentation {
// Display-only camera history. Never derive the room translation from a player's
// predicted position: that transfers prediction reconciliation into the whole view.
struct Point { float x=0, y=0; };
inline bool Finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
inline float ClampAlpha(float alpha) {
    if (!std::isfinite(alpha)) return 0.0f;
    return alpha<0 ? 0.0f : (alpha>1 ? 1.0f : alpha);
}
inline float Alpha(double presentationTime, double lastMainTick, double mainPeriod=1.0/60.0) {
    if (!std::isfinite(presentationTime) || !std::isfinite(lastMainTick) ||
        !std::isfinite(mainPeriod) || mainPeriod<=0) return 0.0f;
    const double a=(presentationTime-lastMainTick)/mainPeriod;
    return a<0 ? 0.0f : (a>1 ? 1.0f : float(a));
}
inline float Axis(float previous, float current, float cameraAlpha) {
    return previous+(current-previous)*ClampAlpha(cameraAlpha);
}
inline Point Interpolate(Point previous, Point current, float cameraAlpha) {
    return {Axis(previous.x,current.x,cameraAlpha),Axis(previous.y,current.y,cameraAlpha)};
}
// Optional pure state helper for unit fixtures. Production can keep the existing
// g_cam.prev/cur/valid and use Axis directly. Scope identity stays host-owned.
struct History {
    Point previous{},current{};
    bool valid=false;
    bool Sample(Point authoritative, bool discontinuity=false, float maximumStep=120.0f) {
        if(!Finite(authoritative)){valid=false;return false;}
        const float dx=authoritative.x-current.x,dy=authoritative.y-current.y;
        const bool fresh=!valid || discontinuity ||
            (maximumStep>0 && dx*dx+dy*dy>maximumStep*maximumStep);
        previous=fresh?authoritative:current;current=authoritative;valid=true;return true;
    }
    Point Present(float cameraAlpha) const {return Interpolate(previous,current,cameraAlpha);}
};
// CameraAlpha follows main-clock samples even when GameFrame is frozen. Pausing,
// menus and transitions must not substitute worldAlpha/playerAlpha for this value.
// Constant native camera samples naturally hold still; lifecycle snaps reset both
// endpoints. This introduces at most one 60Hz tick of camera display delay only.
// Sprite pixel snapping remains native; no new camera rounding or player snap
// compensation is introduced in this conservative candidate.
}}
