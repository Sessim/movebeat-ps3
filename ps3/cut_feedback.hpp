#ifndef MOVEBEAT_PS3_CUT_FEEDBACK_HPP
#define MOVEBEAT_PS3_CUT_FEEDBACK_HPP

#include <cmath>

#include "movebeat/game.hpp"

namespace movebeat {
namespace ps3 {

// How one half of a cut flies, so many halves do not fall in step:
// factors on the spread, fall, travel toward the player and tumble speeds,
// and a slide along the swing (m/s). The defaults are the plain motion.
struct PieceMotion {
    float spread;
    float fall;
    float drift;
    float spin;
    float slide;

    PieceMotion() : spread(1.0f), fall(1.0f), drift(1.0f), spin(1.0f), slide(0.0f) {}
};

struct CutBurst {
    Vec3 position;
    Vec3 swing;
    Hand hand;
    NoteKind kind;
    unsigned int started_ms;
    bool active;
    PieceMotion motion[2];

    CutBurst() : swing(1.0f, 0.0f, 0.0f), hand(HAND_LEFT), kind(NOTE_CUT),
                 started_ms(0), active(false) {}
};

// One half of a cut cube: a rounded box given by its centre and three
// half-extent vectors. axis[0] runs across the cut (the piece's thickness),
// axis[1] along the swing, axis[2] along the track. The cut face is the
// axis[0] face that looks at the other half: on the -axis[0] side when
// `cut_face_positive` is false.
struct CutPiece {
    Vec3 center;
    Vec3 axis[3];
    bool cut_face_positive;
    // 1 = opaque, 0 = gone.
    float opacity;
};

class CutFeedback {
public:
    // The flash is short; the two halves fly apart a little longer and
    // shrink away over the last part of their life.
    enum { MAX_BURSTS = 32, DURATION_MS = 180, PIECE_MS = 450 };

    CutFeedback() : next_(0), random_(0x9e3779b9u) {}

    void clear() {
        for (unsigned int i = 0; i < MAX_BURSTS; ++i) bursts_[i].active = false;
        next_ = 0;
    }

    void add(const FrameEvents& events, unsigned int now_ms) {
        for (unsigned int i = 0; i < events.cut_visual_count; ++i) {
            CutBurst& burst = bursts_[next_];
            burst.position = events.cut_visuals[i].position;
            burst.swing = events.cut_visuals[i].swing;
            burst.hand = events.cut_visuals[i].hand;
            burst.kind = events.cut_visuals[i].kind;
            burst.started_ms = now_ms;
            burst.active = true;
            // Drawn once per cut, so a half keeps its path frame to frame.
            for (int half = 0; half < 2; ++half) {
                PieceMotion& motion = burst.motion[half];
                motion.spread = 0.75f + 0.5f * nextRandom();
                motion.fall = 0.6f + 0.9f * nextRandom();
                motion.drift = 0.7f + 0.6f * nextRandom();
                motion.spin = 0.6f + 0.8f * nextRandom();
                motion.slide = 0.8f * (nextRandom() - 0.5f);
            }
            next_ = (next_ + 1u) % MAX_BURSTS;
        }
    }

    bool visible(unsigned int index, unsigned int now_ms) const {
        return alive(index, now_ms, DURATION_MS);
    }
    bool piecesVisible(unsigned int index, unsigned int now_ms) const {
        return alive(index, now_ms, PIECE_MS);
    }

    const CutBurst& burst(unsigned int index) const { return bursts_[index]; }

private:
    // xorshift32: cheap, and the same sequence on the PC and the PS3.
    float nextRandom() {
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        return (random_ >> 8) * (1.0f / 16777216.0f);
    }

    bool alive(unsigned int index, unsigned int now_ms, unsigned int duration) const {
        return index < MAX_BURSTS && bursts_[index].active &&
               now_ms >= bursts_[index].started_ms &&
               now_ms - bursts_[index].started_ms < duration;
    }

    CutBurst bursts_[MAX_BURSTS];
    unsigned int next_;
    unsigned int random_;
};

// The two halves of `burst` at `now_ms`, for a cube of edge `side`. They
// drift apart across the swing, keep moving toward the player, fall and
// tumble about the swing axis, shrinking a little while they fade out.
// Returns false once they are gone.
inline bool cutPieces(const CutBurst& burst, unsigned int now_ms, float side,
                      CutPiece pieces[2]) {
    if (!burst.active || now_ms < burst.started_ms) return false;
    const unsigned int age_ms = now_ms - burst.started_ms;
    if (age_ms >= static_cast<unsigned int>(CutFeedback::PIECE_MS)) return false;
    const float t = age_ms * 0.001f;
    const float life = CutFeedback::PIECE_MS * 0.001f;
    const float fade_start = life * 0.25f;
    const float opacity = t < fade_start ? 1.0f : 1.0f - (t - fade_start) / (life - fade_start);
    const float size = 1.0f - 0.2f * t / life;
    Vec3 swing = burst.swing;
    swing.z = 0.0f;
    const float swing_length = std::sqrt(swing.x * swing.x + swing.y * swing.y);
    swing = swing_length > 1e-4f ? swing * (1.0f / swing_length) : Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 across(-swing.y, swing.x, 0.0f);
    const Vec3 track(0.0f, 0.0f, 1.0f);
    for (int index = 0; index < 2; ++index) {
        const float sign = index ? 1.0f : -1.0f;
        // Tumble in opposite directions about the swing axis. Both rotated
        // axes stay perpendicular to the swing, so the rotation is a plain
        // turn in the across/track plane.
        const PieceMotion& motion = burst.motion[index];
        const float angle = sign * 9.0f * motion.spin * t;
        const float c = std::cos(angle), s = std::sin(angle);
        const Vec3 turned_across = across * c + track * s;
        const Vec3 turned_track = track * c - across * s;
        CutPiece& piece = pieces[index];
        piece.center = burst.position +
                       across * (sign * (side * 0.25f + 1.1f * motion.spread * t)) +
                       swing * (motion.slide * t) +
                       Vec3(0.0f, -2.0f * motion.fall * t * t, -1.2f * motion.drift * t);
        piece.axis[0] = turned_across * (side * 0.25f * size);
        piece.axis[1] = swing * (side * 0.5f * size);
        piece.axis[2] = turned_track * (side * 0.5f * size);
        piece.cut_face_positive = sign < 0.0f;
        piece.opacity = opacity;
    }
    return opacity > 0.0f;
}

} // namespace ps3
} // namespace movebeat

#endif
