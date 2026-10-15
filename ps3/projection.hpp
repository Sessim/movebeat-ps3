#ifndef MOVEBEAT_PS3_PROJECTION_HPP
#define MOVEBEAT_PS3_PROJECTION_HPP

#include "movebeat/types.hpp"

namespace movebeat {
namespace ps3 {

struct ScreenPoint {
    int x;
    int y;
};

const float kVisibleTrackDepth = 5.0f;
const float kProjectionDepth = 2.0f;
const float kProjectionFocalHeight = 0.34f;
// Center the spectator camera between the upper and lower note rows so their
// real 3D trajectories diverge vertically toward the player instead of all
// sliding down the screen.
const float kProjectionEyeHeight = 0.0f;
// The camera is only slightly above the middle strike row. Pitch is separate:
// it reveals a broad top plane without moving the viewpoint above the notes.
const float kProjectionCameraHeight = 0.13f;
const float kProjectionCameraX = 0.0f;
const float kProjectionPitchOffset = 0.45f;
const float kProjectionTargetDepth = 2.5f;
// Notes and track share the player's camera: front faces point toward the
// player instead of being turned sideways to manufacture a three-quarter view.
const float kNoteCameraX = kProjectionCameraX;
const float kNoteCameraHeight = kProjectionCameraHeight;
const float kNoteCameraPitchOffset = kProjectionPitchOffset;

struct CameraPoint {
    float x;
    float y;
    float depth;
};

// Where the camera stands relative to its home position, in world meters
// (+x right, +y up, +z into the screen). It follows the player's head;
// the camera keeps its orientation, so moving it shows the scene from the
// side (parallax). Zero leaves the classic fixed view.
inline Vec3& cameraOffset() {
    static Vec3 offset(0.0f, 0.0f, 0.0f);
    return offset;
}

inline CameraPoint toCameraSpaceWithOffset(const Vec3& point, float camera_x,
                                           float pitch_offset, const Vec3& offset) {
    const float dx = point.x - camera_x - offset.x;
    const float dy = point.y -
        (kProjectionEyeHeight + kProjectionCameraHeight) - offset.y;
    const float dz = point.z + kProjectionDepth - offset.z;
    const float target_distance = kProjectionDepth + kProjectionTargetDepth;
    const float camera_distance = std::sqrt(
        target_distance * target_distance +
        camera_x * camera_x + pitch_offset * pitch_offset);
    const float yaw_sine = camera_x / std::sqrt(
        target_distance * target_distance +
        camera_x * camera_x);
    const float yaw_cosine = target_distance / std::sqrt(
        target_distance * target_distance +
        camera_x * camera_x);
    const float pitch_sine = pitch_offset / camera_distance;
    const float pitch_cosine = std::sqrt(
        target_distance * target_distance +
        camera_x * camera_x) / camera_distance;

    CameraPoint result;
    result.x = dx * yaw_cosine + dz * yaw_sine;
    result.y = -dx * pitch_sine * yaw_sine + dy * pitch_cosine +
               dz * pitch_sine * yaw_cosine;
    result.depth = -dx * yaw_sine * pitch_cosine - dy * pitch_sine +
                   dz * yaw_cosine * pitch_cosine;
    return result;
}

inline CameraPoint toCameraSpaceFrom(const Vec3& point, float camera_x,
                                     float pitch_offset) {
    return toCameraSpaceWithOffset(point, camera_x, pitch_offset, cameraOffset());
}

// The note camera's world position, for choosing which cube faces it sees.
inline float noteCameraX() { return kNoteCameraX + cameraOffset().x; }
inline float noteCameraEyeY() {
    return kProjectionEyeHeight + kNoteCameraHeight + cameraOffset().y;
}

inline CameraPoint toCameraSpace(const Vec3& point) {
    return toCameraSpaceFrom(point, kProjectionCameraX,
                             kProjectionPitchOffset);
}

inline CameraPoint toNoteCameraSpace(const Vec3& point) {
    return toCameraSpaceFrom(point, kNoteCameraX,
                             kNoteCameraPitchOffset);
}

// Offset the principal point so the player's strike plane (z=0, y=0) remains
// centered even though the camera looks down enough to show real cube depth.
inline float projectionVerticalCenterOffset(int height) {
    // From the home position: the frame must not re-center as the camera
    // moves, or vertical parallax would cancel out.
    const CameraPoint strike = toCameraSpaceWithOffset(
        Vec3(0.0f, 0.0f, 0.0f), kProjectionCameraX, kProjectionPitchOffset,
        Vec3(0.0f, 0.0f, 0.0f));
    const float focal_pixels = height * kProjectionFocalHeight *
                               kProjectionDepth;
    return strike.y * focal_pixels / strike.depth;
}

// Reconstruct camera-space depth where a screen-space glyph intersects a
// world-Z plane. Direction arrows are rasterized after the cube mesh, so their
// depth must vary across the tilted face instead of using one center depth.
inline float cameraDepthAtScreenPointOnWorldZ(float x, float y, int width,
                                               int height, float world_z) {
    const float target_distance = kProjectionDepth + kProjectionTargetDepth;
    const float horizontal_distance = std::sqrt(
        target_distance * target_distance +
        kProjectionCameraX * kProjectionCameraX);
    const float camera_distance = std::sqrt(
        horizontal_distance * horizontal_distance +
        kProjectionPitchOffset * kProjectionPitchOffset);
    const float yaw_sine = kProjectionCameraX / horizontal_distance;
    const float yaw_cosine = target_distance / horizontal_distance;
    const float pitch_sine = kProjectionPitchOffset / camera_distance;
    const float pitch_cosine = horizontal_distance / camera_distance;
    const float focal_pixels = height * kProjectionFocalHeight *
                               kProjectionDepth;
    const float ray_x = (x - width * 0.5f) / focal_pixels;
    const float ray_y = (height * 0.5f +
        projectionVerticalCenterOffset(height) - y) / focal_pixels;
    const float world_z_per_depth =
        ray_x * yaw_sine +
        ray_y * pitch_sine * yaw_cosine +
        pitch_cosine * yaw_cosine;
    if (world_z_per_depth <= 0.0001f) return 0.0f;
    return (world_z + kProjectionDepth - cameraOffset().z) / world_z_per_depth;
}

// A pinhole camera shared by world geometry and software note rendering.
inline ScreenPoint projectWorldPoint(const Vec3& point, int width, int height) {
    CameraPoint camera = toCameraSpace(point);
    if (camera.depth < 0.1f) camera.depth = 0.1f;
    const float focal_pixels = height * kProjectionFocalHeight * kProjectionDepth;
    ScreenPoint result;
    result.x = width / 2 + static_cast<int>(camera.x * focal_pixels / camera.depth);
    result.y = height / 2 + static_cast<int>(projectionVerticalCenterOffset(height)) -
               static_cast<int>(camera.y * focal_pixels / camera.depth);
    return result;
}

inline ScreenPoint projectNotePoint(const Vec3& point, int width, int height) {
    CameraPoint camera = toNoteCameraSpace(point);
    if (camera.depth < 0.1f) camera.depth = 0.1f;
    const float focal_pixels = height * kProjectionFocalHeight *
                               kProjectionDepth;
    ScreenPoint result;
    result.x = width / 2 + static_cast<int>(camera.x * focal_pixels / camera.depth);
    result.y = height / 2 + static_cast<int>(projectionVerticalCenterOffset(height)) -
               static_cast<int>(camera.y * focal_pixels / camera.depth);
    return result;
}

} // namespace ps3
} // namespace movebeat

#endif
