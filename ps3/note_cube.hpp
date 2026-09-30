#ifndef MOVEBEAT_PS3_NOTE_CUBE_HPP
#define MOVEBEAT_PS3_NOTE_CUBE_HPP

#include "projection.hpp"

namespace movebeat {
namespace ps3 {

// Eight vertices of an axis-aligned cube in world space. Its front face is
// perpendicular to the travel axis (Z); note-specific perspective reveals
// its side planes while the projected center stays on the track-camera path.
struct NoteCubeMesh {
    // Four travel-facing corners followed by the matching rear corners.
    Vec3 world[8];
    ScreenPoint screen[8];
    // Front face coincides with the cube edges; no inset frame or fake border.
    Vec3 front[4];
    ScreenPoint front_center;
};

struct NoteCubeVisibleFaces {
    bool left;
    bool right;
    bool top;
    bool bottom;
};

const float kGameplayNoteDepthScale = 1.0f;

inline float gameplayNoteDepth(float side) {
    return side * kGameplayNoteDepthScale;
}

// Emit only side/top/bottom faces visible from the player's shared camera so
// the travel-facing front stays uninterrupted and hidden surfaces never rely
// on depth testing.
inline NoteCubeVisibleFaces visibleNoteCubeFaces(const NoteCubeMesh& cube) {
    NoteCubeVisibleFaces faces;
    faces.left = kNoteCameraX < cube.world[0].x;
    faces.right = kNoteCameraX > cube.world[1].x;
    faces.top = kProjectionEyeHeight + kNoteCameraHeight > cube.world[0].y;
    faces.bottom = kProjectionEyeHeight + kNoteCameraHeight < cube.world[3].y;
    return faces;
}

inline Vec3 noteCubeVertex(const Vec3& center, float side,
                           float local_x, float local_y, float local_z) {
    return Vec3(center.x + local_x * side,
                center.y + local_y * side,
                center.z + local_z * side);
}

inline NoteCubeMesh makeNoteCube(const Vec3& center, float side,
                                 int width, int height, float depth = -1.0f) {
    NoteCubeMesh mesh;
    // Top-left, top-right, bottom-right, bottom-left; rear follows. Gameplay
    // passes the same length as width and height: every note is a true cube,
    // not a shallow frame or an artificially stretched cuboid.
    if (depth <= 0.0f) depth = side;
    const float local[8][3] = {
        {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f},
        { 0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f},
        {-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f},
        { 0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f}
    };
    const ScreenPoint track_center = projectWorldPoint(center, width, height);
    const ScreenPoint note_center = projectNotePoint(center, width, height);
    for (int index = 0; index < 8; ++index) {
        mesh.world[index] = noteCubeVertex(
            center, side, local[index][0], local[index][1],
            local[index][2] * (depth / side));
        const ScreenPoint point = projectNotePoint(mesh.world[index], width, height);
        mesh.screen[index].x = point.x + track_center.x - note_center.x;
        mesh.screen[index].y = point.y + track_center.y - note_center.y;
    }
    for (int index = 0; index < 4; ++index) {
        mesh.front[index] = mesh.world[index];
    }
    mesh.front_center.x = (mesh.screen[0].x + mesh.screen[1].x +
                           mesh.screen[2].x + mesh.screen[3].x) / 4;
    mesh.front_center.y = (mesh.screen[0].y + mesh.screen[1].y +
                           mesh.screen[2].y + mesh.screen[3].y) / 4;
    return mesh;
}

} // namespace ps3
} // namespace movebeat

#endif
