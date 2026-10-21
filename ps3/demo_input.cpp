#include "demo_input.hpp"

namespace movebeat {
namespace ps3 {
namespace {

Vec3 cutDirection(CutDirection direction) {
    switch (direction) {
        case CUT_UP: return Vec3(0.0f, 1.0f, 0.0f);
        case CUT_DOWN: return Vec3(0.0f, -1.0f, 0.0f);
        case CUT_LEFT: return Vec3(-1.0f, 0.0f, 0.0f);
        case CUT_RIGHT: return Vec3(1.0f, 0.0f, 0.0f);
        case CUT_UP_LEFT: return normalized(Vec3(-1.0f, 1.0f, 0.0f));
        case CUT_UP_RIGHT: return normalized(Vec3(1.0f, 1.0f, 0.0f));
        case CUT_DOWN_LEFT: return normalized(Vec3(-1.0f, -1.0f, 0.0f));
        case CUT_DOWN_RIGHT: return normalized(Vec3(1.0f, -1.0f, 0.0f));
        case CUT_ANY: return Vec3(1.0f, 0.0f, 0.0f);
    }
    return Vec3(1.0f, 0.0f, 0.0f);
}

SaberPose scriptedPose(const Game& game, unsigned int now_ms, Hand hand,
                       bool automatic_cuts) {
    const float side = hand == HAND_LEFT ? -1.45f : 1.45f;
    Vec3 tip(side, 1.0f, 0.05f);
    if (!automatic_cuts)
        return SaberPose(tip + Vec3(0.0f, -0.7f, -0.3f), tip, true);

    const float target_side = hand == HAND_LEFT ? -0.58f : 0.58f;
    tip = Vec3(target_side, 1.65f, 0.75f);
    long long closest = 1000000;
    std::size_t chosen = game.noteCount();
    for (std::size_t index = 0; index < game.noteCount(); ++index) {
        if (!game.notePending(index)) continue;
        const Note& note = game.note(index);
        if (note.hand != hand || note.kind == NOTE_BOMB) continue;
        const long long delta = static_cast<long long>(now_ms) - note.time_ms;
        if (delta < -80 || delta > 60) continue;
        const long long distance = delta < 0 ? -delta : delta;
        if (distance < closest) {
            closest = distance;
            chosen = index;
        }
    }
    if (chosen != game.noteCount()) {
        const Note& note = game.note(chosen);
        const long long delta = static_cast<long long>(now_ms) - note.time_ms;
        float progress = static_cast<float>(delta) / 40.0f;
        if (progress < -1.0f) progress = -1.0f;
        if (progress > 1.0f) progress = 1.0f;
        const Vec3 center = game.notePosition(note, note.time_ms);
        tip = center + cutDirection(note.direction) * (progress * 0.46f);
        tip.z = 0.05f;
    } else {
        // Between notes, the blade's tip rides an arc of its hand.
        for (std::size_t index = 0; index < game.arcCount(); ++index) {
            const Arc& arc = game.arc(index);
            if (arc.hand != hand || now_ms < arc.head_time_ms || now_ms > arc.tail_time_ms)
                continue;
            const unsigned int duration = arc.tail_time_ms - arc.head_time_ms;
            const float progress = duration
                ? static_cast<float>(now_ms - arc.head_time_ms) / duration : 1.0f;
            tip = game.arcPoint(arc, now_ms, progress);
            break;
        }
    }
    return SaberPose(tip + Vec3(0.0f, -0.7f, -0.3f), tip, true);
}

} // namespace

MoveFrame scriptedDemoFrame(const Game& game, unsigned int now_ms,
                            bool automatic_cuts) {
    MoveFrame frame;
    frame.left_connected = frame.right_connected = true;
    frame.left = scriptedPose(game, now_ms, HAND_LEFT, automatic_cuts);
    frame.right = scriptedPose(game, now_ms, HAND_RIGHT, automatic_cuts);
    return frame;
}

} // namespace ps3
} // namespace movebeat
