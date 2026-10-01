#ifndef MOVEBEAT_PS3_NOTE_SPAWN_HPP
#define MOVEBEAT_PS3_NOTE_SPAWN_HPP

#include "stage_layout.hpp"
#include "movebeat/game.hpp"

namespace movebeat {
namespace ps3 {

// Visual arrival of a note: it emerges small from the portal at the far
// end of the track, near the portal's center, and settles into its cell
// during the first part of its flight. Upper-row notes therefore rise into
// place and lower-row notes drop into place. Only drawing uses this; the
// game's gameplay positions are never changed.
struct NoteArrival {
    Vec3 position;
    float scale;
};

// Fraction of the spawn depth over which the note settles. The motion is
// over long before the strike zone, where cuts are judged.
const float kArrivalFraction = 0.35f;
// Horizontal distance from the center at the portal, relative to the lane.
const float kArrivalLateralStart = 0.25f;
// Size at the portal, relative to the full note.
const float kArrivalStartScale = 0.2f;
// Closer than this a note is always drawn at its gameplay position and full
// size. Map metadata can put the spawn as close as 0.25 m (see
// configureNoteJump), so the arrival is shortened, or skipped, to keep it
// out of the strike zone.
const float kArrivalSafeDepth = 1.5f;

// gameplay: the game's position; middle_y: the height the note leaves the
// portal at; spawn_depth: the portal's depth.
inline NoteArrival noteArrival(const Vec3& gameplay, float middle_y,
                               float spawn_depth) {
    NoteArrival result;
    result.position = gameplay;
    result.scale = 1.0f;
    float distance = spawn_depth * kArrivalFraction;
    if (spawn_depth - distance < kArrivalSafeDepth)
        distance = spawn_depth - kArrivalSafeDepth;
    if (!(distance > 0.0f)) return result;
    float progress = (spawn_depth - gameplay.z) / distance;
    if (progress >= 1.0f) return result;
    if (progress < 0.0f) progress = 0.0f;
    // Ease out: fast departure from the portal, gentle settling.
    const float rest = 1.0f - progress;
    const float eased = 1.0f - rest * rest * rest;
    const float start_x = gameplay.x * kArrivalLateralStart;
    result.position.x = start_x + (gameplay.x - start_x) * eased;
    result.position.y = middle_y + (gameplay.y - middle_y) * eased;
    // Grow to full size in the first 40% of the arrival.
    float grow = progress / 0.4f;
    if (grow > 1.0f) grow = 1.0f;
    result.scale = kArrivalStartScale + (1.0f - kArrivalStartScale) * grow;
    return result;
}

// Notes leave from the portal's center; the upper row then rises and the
// lower rows drop into their cells.
inline NoteArrival visualNote(const Game& game, const Note& note,
                              unsigned int now_ms) {
    return noteArrival(game.notePosition(note, now_ms), kPortalCenterY,
                       game.noteSpawnDepth());
}

// Arc points follow the same arrival so arcs stay attached to their notes.
inline Vec3 visualArcPoint(const Game& game, const Vec3& point) {
    return noteArrival(point, kPortalCenterY, game.noteSpawnDepth()).position;
}

} // namespace ps3
} // namespace movebeat

#endif
