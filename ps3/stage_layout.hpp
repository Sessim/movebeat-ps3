#ifndef MOVEBEAT_PS3_STAGE_LAYOUT_HPP
#define MOVEBEAT_PS3_STAGE_LAYOUT_HPP

#include <cmath>

namespace movebeat {
namespace ps3 {

// Background stage: neon frames drift from behind the portal toward the
// player, and the stage pulses on the beat. A handful of frames keeps the
// background to a few hundred vertices of thin additive bands.
const unsigned int kTunnelRingCount = 7;
const float kTunnelRingSpacing = 1.6f;
const float kTunnelSpeed = 1.2f;            // meters per second
const float kTunnelBeyondPortal = 4.0f;     // farthest frame behind the portal
const float kTunnelFadeInMeters = 2.0f;
const float kTunnelFadeOutZ = 2.0f;         // start fading at this depth
const float kTunnelVanishZ = 0.3f;          // invisible from here on
// Frame size around the track (world meters).
const float kTunnelHalfWidth = 2.4f;
const float kTunnelBottom = -0.825f;
const float kTunnelTop = 2.6f;

// The portal in the far wall (world meters, at the note spawn depth).
const float kPortalHalfWidth = 1.1f;
const float kPortalBottom = -0.825f;
const float kPortalTop = 1.1f;
const float kPortalCenterY = (kPortalBottom + kPortalTop) * 0.5f;

struct TunnelRing {
    float z;
    float alpha;
};

inline TunnelRing tunnelRing(unsigned int index, unsigned int now_ms,
                             float far_z) {
    const double span = kTunnelRingCount * kTunnelRingSpacing;
    double travel = std::fmod(now_ms * 0.001 * kTunnelSpeed +
                              index * kTunnelRingSpacing, span);
    if (travel < 0.0) travel += span;
    TunnelRing ring;
    ring.z = static_cast<float>(far_z + kTunnelBeyondPortal - travel);
    float alpha = static_cast<float>(travel / kTunnelFadeInMeters);
    if (alpha > 1.0f) alpha = 1.0f;
    if (ring.z < kTunnelFadeOutZ) {
        const float out = (ring.z - kTunnelVanishZ) /
                          (kTunnelFadeOutZ - kTunnelVanishZ);
        if (out < alpha) alpha = out;
    }
    ring.alpha = alpha < 0.0f ? 0.0f : alpha;
    return ring;
}

// 1 on the beat, decaying quickly to 0 before the next one.
inline float beatPulse(unsigned int song_ms, float bpm) {
    if (!(bpm > 0.0f) || bpm > 1000.0f) return 0.0f;
    const double beat_ms = 60000.0 / bpm;
    const double phase = std::fmod(static_cast<double>(song_ms), beat_ms) /
                         beat_ms;
    const float rest = static_cast<float>(1.0 - phase);
    return rest * rest * rest;
}

} // namespace ps3
} // namespace movebeat

#endif
