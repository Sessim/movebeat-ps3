#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <dirent.h>
#include <string.h>
#include <string>
#include <vector>

#include <sys/systime.h>
#include <sysutil/sysutil.h>

#include "framebuffer.hpp"
#include "frame_timing.hpp"
#include "cut_feedback.hpp"
#include "library_card.hpp"
#include "library_window.hpp"
#include "menu_pointer.hpp"
#include "note_cube.hpp"
#include "projection.hpp"
#include "gem_runtime.hpp"
#include "hand_assignment.hpp"
#include "menu_buttons.hpp"
#include "tracking_pause.hpp"
#include "demo_input.hpp"
#include "audio_player.hpp"
#include "run_diagnostics.hpp"
#include "movebeat/game.hpp"
#include "movebeat/calibration.hpp"
#include "movebeat/level.hpp"
#include "movebeat/records.hpp"
#include "movebeat/settings.hpp"

namespace {

volatile bool g_running = true;
volatile bool g_pause_requested = false;

const char kLevelRoot[] = "/dev_hdd0/game/MOVEBEAT1/USRDIR/levels";
const char kScoreLog[] = "/dev_hdd0/game/MOVEBEAT1/USRDIR/scores.log";
const char kSettingsPath[] = "/dev_hdd0/game/MOVEBEAT1/USRDIR/settings.ini";
const char kCapturePath[] = "/dev_hdd0/game/MOVEBEAT1/USRDIR/gem_capture.csv";
const char kStartupReportPath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/startup_error.txt";
const char kRunStatusPath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/run_status.ini";
const char kFirstFramePath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/first_frame.ppm";
const char kDemoModePath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/demo_mode.ini";
const char kDemoFramePath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/demo_frame.ppm";
const char kDemoScoredFramePath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/demo_scored_frame.ppm";
const unsigned short kButtonStart = 1u << 3;
const unsigned short kButtonSelect = 1u << 0;
const unsigned short kButtonTriangle = 1u << 4;
const unsigned short kButtonCircle = 1u << 5;
const unsigned short kButtonCross = 1u << 6;
const unsigned short kButtonSquare = 1u << 7;

int roundPixel(float value) {
    return static_cast<int>(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

enum RunState {
    RUN_SELECT,
    RUN_PLAY,
    RUN_RESULTS,
    RUN_AUDIO_ERROR
};

void systemEvent(u64 status, u64, void*) {
    if (status == SYSUTIL_EXIT_GAME) g_running = false;
    else if (status == SYSUTIL_MENU_OPEN)
        g_pause_requested = true;
    else if (status == SYSUTIL_MENU_CLOSE)
        g_pause_requested = false;
}

int clamp(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

struct EmulatorDemoOptions {
    bool enabled;
    bool automatic_cuts;
    bool tracking_loss_test;
    std::string level_id;
    unsigned int chart_index;

    EmulatorDemoOptions() : enabled(false), automatic_cuts(true),
                            tracking_loss_test(false),
                            chart_index(0) {}
};

bool parseDemoChartIndex(const char* text, unsigned int& index) {
    if (!text || !*text) return false;
    unsigned int parsed = 0;
    for (const char* digit = text; *digit; ++digit) {
        if (*digit < '0' || *digit > '9') return false;
        parsed = parsed * 10u + static_cast<unsigned int>(*digit - '0');
        if (parsed > 63u) return false;
    }
    index = parsed;
    return true;
}

EmulatorDemoOptions readEmulatorDemoOptions() {
    EmulatorDemoOptions options;
    FILE* file = 0;
#if defined(_MSC_VER)
    fopen_s(&file, kDemoModePath, "rb");
#else
    file = fopen(kDemoModePath, "rb");
#endif
    if (!file) return options;
    char line[128] = {};
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "enabled=1") == 0) options.enabled = true;
        else if (strcmp(line, "automatic_cuts=0") == 0)
            options.automatic_cuts = false;
        else if (strcmp(line, "tracking_loss_test=1") == 0)
            options.tracking_loss_test = true;
        else if (strncmp(line, "level_id=", 9) == 0 &&
                 strlen(line + 9) <= 64)
            options.level_id = line + 9;
        else if (strncmp(line, "chart_index=", 12) == 0)
            parseDemoChartIndex(line + 12, options.chart_index);
    }
    fclose(file);
    return options;
}

unsigned int adjustedTime(unsigned int raw_time_ms, int offset_ms) {
    const long long adjusted = static_cast<long long>(raw_time_ms) + offset_ms;
    if (adjusted <= 0) return 0;
    const unsigned int maximum = ~0u;
    if (adjusted > static_cast<long long>(maximum)) return maximum;
    return static_cast<unsigned int>(adjusted);
}

struct LevelChoice {
    std::string directory;
    movebeat::LevelManifest manifest;
    movebeat::ps3::LibraryCard card;
};

bool levelChoiceLess(const LevelChoice& left, const LevelChoice& right) {
    const bool left_current = left.directory == std::string(kLevelRoot) + "/current";
    const bool right_current = right.directory == std::string(kLevelRoot) + "/current";
    if (left_current != right_current) return left_current;
    if (left.manifest.title != right.manifest.title)
        return left.manifest.title < right.manifest.title;
    return left.directory < right.directory;
}

std::vector<LevelChoice> discoverLevels() {
    std::vector<LevelChoice> choices;
    DIR* directory = opendir(kLevelRoot);
    if (!directory) return choices;
    for (dirent* entry = readdir(directory); entry && choices.size() < 64;
         entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (name.empty() || name == "." || name == ".." ||
            name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos) continue;
        LevelChoice choice;
        choice.directory = std::string(kLevelRoot) + "/" + name;
        std::string error;
        movebeat::Chart validation_chart;
        if (movebeat::loadLevelDirectory(choice.directory, choice.manifest, error) &&
            movebeat::loadLevelChart(choice.directory, choice.manifest, 0,
                                     validation_chart, error)) {
            choices.push_back(choice);
        }
    }
    closedir(directory);
    std::sort(choices.begin(), choices.end(), levelChoiceLess);
    return choices;
}

void updateLibraryCards(std::vector<LevelChoice>& levels, std::size_t selected) {
    for (std::size_t index = 0; index < levels.size(); ++index) {
        if (movebeat::ps3::libraryCardVisible(index, selected, levels.size())) {
            if (!levels[index].card.ready())
                levels[index].card.load(levels[index].directory);
        } else {
            levels[index].card.clear();
        }
    }
}

void recordLibraryCardMemory(movebeat::ps3::RunStatus& diagnostic,
                             const std::vector<LevelChoice>& levels) {
    unsigned int loaded = 0;
    for (std::size_t index = 0; index < levels.size(); ++index)
        if (levels[index].card.ready()) ++loaded;
    diagnostic.library_cards_loaded = loaded;
    diagnostic.library_card_bytes = loaded *
        movebeat::ps3::LibraryCard::WIDTH *
        movebeat::ps3::LibraryCard::HEIGHT * sizeof(unsigned short);
}

using movebeat::ps3::ScreenPoint;

void drawGrid(movebeat::ps3::Framebuffer& screen, float far_z) {
    const u32 grid = 0xff20283cu;
    const float ground_y = -0.825f;
    const float lane_spacing = 0.55f;
    for (int lane = 0; lane <= 4; ++lane) {
        const float x = (lane - 2) * lane_spacing;
        const ScreenPoint near = movebeat::ps3::projectWorldPoint(
            movebeat::Vec3(x, ground_y, 0.0f), screen.width(), screen.height());
        const ScreenPoint far = movebeat::ps3::projectWorldPoint(
            movebeat::Vec3(x, ground_y, far_z), screen.width(), screen.height());
        screen.line(near.x, near.y, far.x, far.y, grid, 2);
    }
    for (int step = 0; step <= 4; ++step) {
        const float z = far_z * step / 4.0f;
        const ScreenPoint left = movebeat::ps3::projectWorldPoint(
            movebeat::Vec3(-2.0f * lane_spacing, ground_y, z),
            screen.width(), screen.height());
        const ScreenPoint right = movebeat::ps3::projectWorldPoint(
            movebeat::Vec3(2.0f * lane_spacing, ground_y, z),
            screen.width(), screen.height());
        const u32 color = step == 4 ? 0xff506080u : grid;
        screen.line(left.x, left.y, right.x, right.y, color, step == 4 ? 3 : 2);
    }
    // A visible far frame marks the finite note spawn plane.
    const float outer_x = 2.0f * lane_spacing;
    const float top_y = 1.1f;
    const ScreenPoint far_top_left = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(-outer_x, top_y, far_z), screen.width(), screen.height());
    const ScreenPoint far_top_right = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(outer_x, top_y, far_z), screen.width(), screen.height());
    const ScreenPoint far_bottom_left = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(-outer_x, ground_y, far_z), screen.width(), screen.height());
    const ScreenPoint far_bottom_right = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(outer_x, ground_y, far_z), screen.width(), screen.height());
    screen.line(far_bottom_left.x, far_bottom_left.y,
                far_top_left.x, far_top_left.y, 0xff303c58u, 2);
    screen.line(far_top_left.x, far_top_left.y,
                far_top_right.x, far_top_right.y, 0xff303c58u, 2);
    screen.line(far_top_right.x, far_top_right.y,
                far_bottom_right.x, far_bottom_right.y, 0xff303c58u, 2);
}

ScreenPoint projectTrackPoint(const movebeat::Vec3& point,
                              const movebeat::ps3::Framebuffer& screen) {
    return movebeat::ps3::projectWorldPoint(point, screen.width(), screen.height());
}

void drawObstacleFace(movebeat::ps3::Framebuffer& screen,
                      const movebeat::Vec3& minimum,
                      const movebeat::Vec3& maximum,
                      float z, u32 color) {
    const ScreenPoint bottom_left = projectTrackPoint(
        movebeat::Vec3(minimum.x, minimum.y, z), screen);
    const ScreenPoint bottom_right = projectTrackPoint(
        movebeat::Vec3(maximum.x, minimum.y, z), screen);
    const ScreenPoint top_left = projectTrackPoint(
        movebeat::Vec3(minimum.x, maximum.y, z), screen);
    const ScreenPoint top_right = projectTrackPoint(
        movebeat::Vec3(maximum.x, maximum.y, z), screen);
    screen.line(bottom_left.x, bottom_left.y, bottom_right.x, bottom_right.y, color, 4);
    screen.line(bottom_right.x, bottom_right.y, top_right.x, top_right.y, color, 4);
    screen.line(top_right.x, top_right.y, top_left.x, top_left.y, color, 4);
    screen.line(top_left.x, top_left.y, bottom_left.x, bottom_left.y, color, 4);
}

void drawObstacles(movebeat::ps3::Framebuffer& screen, const movebeat::Game& game,
                   unsigned int now_ms) {
    const float far_z = game.noteSpawnDepth();
    for (std::size_t index = 0; index < game.obstacleCount(); ++index) {
        if (!game.obstaclePending(index)) continue;
        const movebeat::Obstacle& obstacle = game.obstacle(index);
        movebeat::Vec3 minimum = game.obstacleMinimum(obstacle, now_ms);
        movebeat::Vec3 maximum = game.obstacleMaximum(obstacle, now_ms);
        if (maximum.z < 0.0f || minimum.z > far_z) continue;
        if (minimum.z < 0.0f) minimum.z = 0.0f;
        if (maximum.z > far_z) maximum.z = far_z;
        const u32 color = minimum.z <= 0.01f ? 0xffff5088u : 0xffa040d0u;
        drawObstacleFace(screen, minimum, maximum, minimum.z, color);
        drawObstacleFace(screen, minimum, maximum, maximum.z, 0xff603080u);
        const ScreenPoint near_bottom_left = projectTrackPoint(
            movebeat::Vec3(minimum.x, minimum.y, minimum.z), screen);
        const ScreenPoint near_bottom_right = projectTrackPoint(
            movebeat::Vec3(maximum.x, minimum.y, minimum.z), screen);
        const ScreenPoint near_top_left = projectTrackPoint(
            movebeat::Vec3(minimum.x, maximum.y, minimum.z), screen);
        const ScreenPoint near_top_right = projectTrackPoint(
            movebeat::Vec3(maximum.x, maximum.y, minimum.z), screen);
        const ScreenPoint far_bottom_left = projectTrackPoint(
            movebeat::Vec3(minimum.x, minimum.y, maximum.z), screen);
        const ScreenPoint far_bottom_right = projectTrackPoint(
            movebeat::Vec3(maximum.x, minimum.y, maximum.z), screen);
        const ScreenPoint far_top_left = projectTrackPoint(
            movebeat::Vec3(minimum.x, maximum.y, maximum.z), screen);
        const ScreenPoint far_top_right = projectTrackPoint(
            movebeat::Vec3(maximum.x, maximum.y, maximum.z), screen);
        screen.line(near_bottom_left.x, near_bottom_left.y,
                    far_bottom_left.x, far_bottom_left.y, color, 3);
        screen.line(near_bottom_right.x, near_bottom_right.y,
                    far_bottom_right.x, far_bottom_right.y, color, 3);
        screen.line(near_top_left.x, near_top_left.y,
                    far_top_left.x, far_top_left.y, color, 3);
        screen.line(near_top_right.x, near_top_right.y,
                    far_top_right.x, far_top_right.y, color, 3);
    }
}

void drawArcs(movebeat::ps3::Framebuffer& screen, const movebeat::Game& game,
              unsigned int now_ms) {
    const unsigned int segments = 16;
    for (std::size_t index = 0; index < game.arcCount(); ++index) {
        const movebeat::Arc& arc = game.arc(index);
        movebeat::Vec3 previous = game.arcPoint(arc, now_ms, 0.0f);
        const u32 core = arc.hand == movebeat::HAND_LEFT
            ? 0xff70d8ffu : 0xffff6878u;
        const u32 glow = arc.hand == movebeat::HAND_LEFT
            ? 0xff174870u : 0xff701727u;
        for (unsigned int segment = 1; segment <= segments; ++segment) {
            const float progress = static_cast<float>(segment) /
                static_cast<float>(segments);
            const movebeat::Vec3 current = game.arcPoint(arc, now_ms, progress);
            if (previous.z >= -0.8f && previous.z <= game.noteSpawnDepth() &&
                current.z >= -0.8f && current.z <= game.noteSpawnDepth()) {
                const ScreenPoint from = projectTrackPoint(previous, screen);
                const ScreenPoint to = projectTrackPoint(current, screen);
                screen.line(from.x, from.y, to.x, to.y, glow, 7);
                screen.line(from.x, from.y, to.x, to.y, core, 2);
            }
            previous = current;
        }
    }
}

void drawSaber(movebeat::ps3::Framebuffer& screen, const movebeat::SaberPose& saber,
               u32 core, u32 glow) {
    if (!saber.tracked) return;
    const ScreenPoint handle = projectTrackPoint(saber.handle, screen);
    const ScreenPoint tip = projectTrackPoint(saber.tip, screen);
    screen.line(handle.x, handle.y, tip.x, tip.y, glow, 15);
    screen.line(handle.x, handle.y, tip.x, tip.y, core, 5);
    screen.circle(handle.x, handle.y, 11, 0xffe8e8f0u);
}

void drawConnection(movebeat::ps3::Framebuffer& screen, int index,
                    bool connected, bool tracked, u32 color) {
    const int size = screen.height() / 30;
    const int x = screen.width() / 40 + index * (size + screen.width() / 80);
    const int y = screen.height() / 30;
    const u32 status_color = tracked ? color : (connected ? 0xffffa020u : 0xff502020u);
    screen.rectangle(x, y, size, size, status_color);
}

movebeat::SaberPose translated(const movebeat::SaberPose& pose,
                               const movebeat::Vec3& origin) {
    if (!pose.tracked) return pose;
    return movebeat::SaberPose(pose.handle - origin, pose.tip - origin, true);
}

movebeat::Note makeNote(unsigned int time_ms, int lane, int row,
                        movebeat::Hand hand, movebeat::CutDirection direction,
                        movebeat::NoteKind kind = movebeat::NOTE_CUT) {
    movebeat::Note note;
    note.time_ms = time_ms;
    note.lane = lane;
    note.row = row;
    note.hand = hand;
    note.direction = direction;
    note.kind = kind;
    return note;
}

movebeat::Chart tutorialChart() {
    movebeat::Chart chart;
    chart.notes.push_back(makeNote(2000, 1, 1, movebeat::HAND_LEFT, movebeat::CUT_UP));
    chart.notes.push_back(makeNote(2600, 2, 1, movebeat::HAND_RIGHT, movebeat::CUT_UP));
    chart.notes.push_back(makeNote(3200, 0, 0, movebeat::HAND_LEFT, movebeat::CUT_RIGHT));
    chart.notes.push_back(makeNote(3800, 3, 2, movebeat::HAND_RIGHT, movebeat::CUT_LEFT));
    chart.notes.push_back(makeNote(4400, 1, 1, movebeat::HAND_NONE, movebeat::CUT_ANY,
                                   movebeat::NOTE_BOMB));
    chart.notes.push_back(makeNote(5000, 2, 0, movebeat::HAND_RIGHT,
                                   movebeat::CUT_UP_LEFT));
    chart.notes.push_back(makeNote(5500, 3, 1, movebeat::HAND_RIGHT,
                                   movebeat::CUT_ANY, movebeat::NOTE_CHAIN));
    movebeat::Arc arc;
    arc.head_time_ms = 2600;
    arc.tail_time_ms = 3800;
    arc.head_lane = 2;
    arc.head_row = 1;
    arc.tail_lane = 3;
    arc.tail_row = 2;
    arc.hand = movebeat::HAND_RIGHT;
    chart.arcs.push_back(arc);
    return chart;
}

unsigned long long nowMicroseconds() {
    u64 seconds = 0;
    u64 nanoseconds = 0;
    sysGetCurrentTime(&seconds, &nanoseconds);
    return seconds * 1000000ull + nanoseconds / 1000ull;
}

u32 shadeNote(u32 color, int numerator, int denominator) {
    const unsigned int red = std::min(255u, ((color >> 16) & 255u) * numerator / denominator);
    const unsigned int green = std::min(255u, ((color >> 8) & 255u) * numerator / denominator);
    const unsigned int blue = std::min(255u, (color & 255u) * numerator / denominator);
    return 0xff000000u | (red << 16) | (green << 8) | blue;
}

void drawNoteCube(movebeat::ps3::Framebuffer& screen,
                  const movebeat::ps3::NoteCubeMesh& cube, u32 color) {
    const ScreenPoint* v = cube.screen;
    // Project and shade the real camera-facing cube planes; don't draw a
    // rear-face rim that makes the note look like a framed flat square.
    const movebeat::ps3::NoteCubeVisibleFaces faces =
        movebeat::ps3::visibleNoteCubeFaces(cube);
    if (faces.top)
        screen.quadrilateral(v[0].x, v[0].y, v[1].x, v[1].y,
                             v[5].x, v[5].y, v[4].x, v[4].y,
                             shadeNote(color, 3, 4));
    else if (faces.bottom)
        screen.quadrilateral(v[3].x, v[3].y, v[2].x, v[2].y,
                             v[6].x, v[6].y, v[7].x, v[7].y,
                             shadeNote(color, 1, 3));
    if (faces.left)
        screen.quadrilateral(v[4].x, v[4].y, v[0].x, v[0].y,
                             v[3].x, v[3].y, v[7].x, v[7].y,
                             shadeNote(color, 1, 3));
    else if (faces.right)
        screen.quadrilateral(v[1].x, v[1].y, v[5].x, v[5].y,
                             v[6].x, v[6].y, v[2].x, v[2].y,
                             shadeNote(color, 2, 5));
    screen.quadrilateral(v[0].x, v[0].y, v[1].x, v[1].y,
                         v[2].x, v[2].y, v[3].x, v[3].y, color);
    // Face shading carries the depth cue; no border is painted over the front.
}

void drawNotes(movebeat::ps3::Framebuffer& screen, const movebeat::Game& game,
               unsigned int now_ms, bool draw_software_faces) {
    const float far_z = game.noteSpawnDepth();
    // The chart is time-ordered, so draw distant notes first for correct overlap.
    for (std::size_t index = game.noteCount(); index-- > 0;) {
        if (!game.notePending(index)) continue;
        const movebeat::Note& note = game.note(index);
        const movebeat::Vec3 position = game.notePosition(note, now_ms);
        if (position.z < -0.8f || position.z > far_z) continue;
        float depth_scale = 1.0f - position.z / movebeat::ps3::kProjectionDepth;
        if (depth_scale < 0.0f) depth_scale = 0.0f;
        if (depth_scale > 1.0f) depth_scale = 1.0f;
        const ScreenPoint projected = projectTrackPoint(position, screen);
        int x = projected.x;
        int y = projected.y;
        int size = std::max(8, static_cast<int>(
            screen.height() * 0.30f * 0.32f * depth_scale));
        const u32 color = note.kind == movebeat::NOTE_BOMB ? 0xff303038u :
            (note.hand == movebeat::HAND_LEFT ? 0xff2098e8u : 0xffe83048u);
        if (note.kind == movebeat::NOTE_BOMB) {
            const float bomb_front_z = position.z - 0.16f;
        const float bomb_front_depth =
                movebeat::ps3::toNoteCameraSpace(movebeat::Vec3(
                    position.x, position.y, bomb_front_z)).depth;
            screen.noteCircle(x, y, size / 2, 0xff60606cu, bomb_front_depth);
            screen.noteCircle(x, y, size / 2 - 2, color, bomb_front_depth);
            screen.noteCircle(x, y, size / 5, 0xffd0d0d8u, bomb_front_depth);
            continue;
        }
        const float side = note.kind == movebeat::NOTE_CHAIN ? 0.16f : 0.32f;
        const float depth = movebeat::ps3::gameplayNoteDepth(side);
        const float face_z = position.z - depth * 0.5f;
        const float face_depth = movebeat::ps3::toNoteCameraSpace(
            movebeat::Vec3(position.x, position.y, face_z)).depth;
        const movebeat::ps3::NoteCubeMesh cube = movebeat::ps3::makeNoteCube(
            position, side, screen.width(), screen.height(), depth);
        if (draw_software_faces) drawNoteCube(screen, cube, color);
        x = cube.front_center.x;
        y = cube.front_center.y;
        const int face_width = cube.screen[1].x - cube.screen[0].x;
        const int face_height = cube.screen[3].y - cube.screen[0].y;
        size = std::max(4, std::min(face_width, face_height));
        if (note.kind == movebeat::NOTE_CHAIN) {
            screen.noteCircle(x, y, size / 3 + 1, 0xfff0f0f0u, face_depth);
        } else if (note.direction == movebeat::CUT_ANY) {
            screen.noteCircle(x, y, size / 8 + 1, 0xfff0f0f0u, face_depth);
        } else {
            int dx = 0;
            int dy = 0;
            switch (note.direction) {
                case movebeat::CUT_UP: dy = -1; break;
                case movebeat::CUT_DOWN: dy = 1; break;
                case movebeat::CUT_LEFT: dx = -1; break;
                case movebeat::CUT_RIGHT: dx = 1; break;
                case movebeat::CUT_UP_LEFT: dx = -1; dy = -1; break;
                case movebeat::CUT_UP_RIGHT: dx = 1; dy = -1; break;
                case movebeat::CUT_DOWN_LEFT: dx = -1; dy = 1; break;
                case movebeat::CUT_DOWN_RIGHT: dx = 1; dy = 1; break;
                case movebeat::CUT_ANY: break;
            }
            const float radians = std::fmod(note.angle_offset_degrees, 360.0f) *
                                  0.01745329251994329577f;
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            float direction_x = dx * cosine + dy * sine;
            float direction_y = -dx * sine + dy * cosine;
            const float magnitude = std::sqrt(direction_x * direction_x +
                                              direction_y * direction_y);
            if (magnitude > 0.0f) {
                direction_x /= magnitude;
                direction_y /= magnitude;
            }
            const int arrow_length = size / 3;
            const int tail_x = roundPixel(
                x - direction_x * arrow_length / 2.0f);
            const int tail_y = roundPixel(
                y - direction_y * arrow_length / 2.0f);
            const int head_x = roundPixel(
                x + direction_x * arrow_length / 2.0f);
            const int head_y = roundPixel(
                y + direction_y * arrow_length / 2.0f);
            const float side_x = -direction_y;
            const float side_y = direction_x;
            const int wing = size / 7 + 1;
            screen.noteLine(tail_x, tail_y, head_x, head_y,
                        0xfff0f0f0u, 3, face_depth);
            screen.noteLine(head_x, head_y,
                        roundPixel(head_x - direction_x * wing + side_x * wing),
                        roundPixel(head_y - direction_y * wing + side_y * wing),
                        0xfff0f0f0u, 3, face_depth);
            screen.noteLine(head_x, head_y,
                        roundPixel(head_x - direction_x * wing - side_x * wing),
                        roundPixel(head_y - direction_y * wing - side_y * wing),
                        0xfff0f0f0u, 3, face_depth);
        }
    }
}

void drawCutFeedback(movebeat::ps3::Framebuffer& screen,
                     const movebeat::ps3::CutFeedback& feedback,
                     unsigned int now_ms) {
    static const int rays[8][2] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1},
        {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
    };
    for (unsigned int index = 0;
         index < movebeat::ps3::CutFeedback::MAX_BURSTS; ++index) {
        if (!feedback.visible(index, now_ms)) continue;
        const movebeat::ps3::CutBurst& burst = feedback.burst(index);
        const unsigned int age = now_ms - burst.started_ms;
        const float progress = static_cast<float>(age) /
            movebeat::ps3::CutFeedback::DURATION_MS;
        const float outer = 0.06f + progress * 0.30f;
        const float inner = outer * 0.52f;
        const float z = burst.position.z - progress * 0.08f;
        const u32 base = burst.hand == movebeat::HAND_LEFT
            ? 0xff70d8ffu : 0xffff6878u;
        const u32 color = shadeNote(base, 10 - static_cast<int>(age * 7u /
            movebeat::ps3::CutFeedback::DURATION_MS), 10);
        for (int ray = 0; ray < 8; ++ray) {
            const float dx = rays[ray][0] * 0.65f;
            const float dy = static_cast<float>(rays[ray][1]);
            const ScreenPoint from = projectTrackPoint(movebeat::Vec3(
                burst.position.x + dx * inner,
                burst.position.y + dy * inner, z), screen);
            const ScreenPoint to = projectTrackPoint(movebeat::Vec3(
                burst.position.x + dx * outer,
                burst.position.y + dy * outer, z), screen);
            screen.line(from.x, from.y, to.x, to.y, color, 2);
        }
        if (age < 55u) {
            const ScreenPoint center = projectTrackPoint(burst.position, screen);
            screen.circle(center.x, center.y, 3, 0xfff5f6ffu);
        }
    }
}

void drawMenuText(movebeat::ps3::Framebuffer& screen, const char* label,
                  int x, int y, int scale, u32 color) {
    static const unsigned char glyphs[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,14}, {17,17,17,31,17,17,17},
        {31,4,4,4,4,4,31}, {7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
    };
    for (const char* letter = label; *letter; ++letter, x += scale * 6) {
        if (*letter < 'A' || *letter > 'Z') continue;
        const unsigned char* rows = glyphs[*letter - 'A'];
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if (rows[row] & (1u << (4 - column)))
                    screen.rectangle(x + column * scale, y + row * scale,
                                     scale, scale, color);
    }
}

void drawCenteredMenuText(movebeat::ps3::Framebuffer& screen,
                          const char* label, int y, int scale, u32 color) {
    const int width = static_cast<int>(strlen(label)) * scale * 6;
    drawMenuText(screen, label, (screen.width() - width) / 2,
                 y, scale, color);
}

const char* startupFailureHint(movebeat::ps3::GemRuntime::InitStage stage,
                               int error) {
    if (stage == movebeat::ps3::GemRuntime::INIT_CAMERA_TYPE && error == -1003)
        return "PS EYE NOT FOUND";
    if (stage >= movebeat::ps3::GemRuntime::INIT_CAMERA &&
        stage <= movebeat::ps3::GemRuntime::INIT_CAMERA_START)
        return "CHECK PS EYE USB";
    if (stage == movebeat::ps3::GemRuntime::INIT_CAMERA_MODULE ||
        stage == movebeat::ps3::GemRuntime::INIT_GEM_MODULE)
        return "SYSTEM MODULE ERROR";
    return "MOVE ENGINE ERROR";
}

void drawNumber(movebeat::ps3::Framebuffer& screen, unsigned int value,
                int center_x, int y, int scale, u32 color);
void drawDigit(movebeat::ps3::Framebuffer& screen, unsigned int digit,
               int x, int y, int scale, u32 color);

std::string menuLabel(const std::string& value) {
    std::string label;
    for (std::size_t index = 0; index < value.size() && label.size() < 18;
         ++index) {
        const char letter = value[index];
        if (letter >= 'a' && letter <= 'z')
            label.push_back(static_cast<char>(letter - 'a' + 'A'));
        else if ((letter >= 'A' && letter <= 'Z') || letter == ' ')
            label.push_back(letter);
    }
    return label.empty() ? "UNKNOWN" : label;
}

void drawTime(movebeat::ps3::Framebuffer& screen, unsigned int milliseconds,
              int x, int y, int scale, u32 color) {
    unsigned int seconds = milliseconds / 1000u;
    unsigned int minutes = seconds / 60u;
    seconds %= 60u;
    if (minutes > 99u) minutes = 99u;
    const unsigned int digits[4] = {
        minutes / 10u, minutes % 10u, seconds / 10u, seconds % 10u
    };
    const int advance = scale * 4;
    for (int index = 0; index < 4; ++index) {
        const int position = index < 2 ? index : index + 1;
        drawDigit(screen, digits[index], x + position * advance, y, scale, color);
    }
    const int colon_x = x + 2 * advance;
    screen.rectangle(colon_x + scale, y + scale, scale, scale, color);
    screen.rectangle(colon_x + scale, y + scale * 3, scale, scale, color);
}

unsigned int estimateChartDurationMs(const movebeat::Chart& chart) {
    unsigned int last_event_ms = 0;
    for (std::size_t index = 0; index < chart.notes.size(); ++index)
        last_event_ms = std::max(last_event_ms, chart.notes[index].time_ms);
    for (std::size_t index = 0; index < chart.obstacles.size(); ++index) {
        const movebeat::Obstacle& obstacle = chart.obstacles[index];
        last_event_ms = std::max(last_event_ms,
            obstacle.time_ms + obstacle.duration_ms);
    }
    for (std::size_t index = 0; index < chart.arcs.size(); ++index)
        last_event_ms = std::max(last_event_ms, chart.arcs[index].tail_time_ms);
    return last_event_ms > 0xffffffffu - 2000u
        ? 0xffffffffu : last_event_ms + 2000u;
}

void drawHud(movebeat::ps3::Framebuffer& screen,
             const movebeat::ScoreState& score,
             unsigned int elapsed_ms, unsigned int duration_ms) {
    const int panel_width = screen.width() / 4;
    const int panel_height = screen.height() / 10;
    const int panel_y = screen.height() / 11;
    const int edge = screen.width() / 48;
    const int label_scale = std::max(2, screen.height() / 360);
    const int value_scale = std::max(3, screen.height() / 145);
    const int label_y = panel_y + screen.height() / 90;
    const int value_y = panel_y + screen.height() / 30;
    const u32 panel = 0xff101827u;
    const u32 label = 0xff9cb3d3u;

    screen.rectangle(edge, panel_y, panel_width, panel_height, panel);
    screen.rectangle(screen.width() - edge - panel_width, panel_y,
                     panel_width, panel_height, panel);
    drawMenuText(screen, "SCORE", edge + screen.width() / 80,
                 label_y, label_scale, label);
    drawNumber(screen, score.score, edge + panel_width / 2,
               value_y, value_scale,
               score.failed ? 0xffff5060u : 0xfff0d050u);
    const int combo_x = screen.width() - edge - panel_width;
    drawMenuText(screen, "COMBO", combo_x + screen.width() / 80,
                 label_y, label_scale, label);
    drawNumber(screen, score.combo, combo_x + panel_width / 2,
               value_y, value_scale, 0xff70d8ffu);

    const int bar_x = screen.width() / 4;
    const int bar_width = screen.width() / 2;
    const int bar_height = std::max(12, screen.height() / 60);
    const int bar_y = screen.height() - screen.height() / 28;
    const int energy_y = bar_y - std::max(8, screen.height() / 90);
    screen.rectangle(bar_x, energy_y, bar_width,
                     std::max(4, screen.height() / 180), 0xff202838u);
    const u32 energy_color = score.energy > 50 ? 0xff40d870u :
        (score.energy > 20 ? 0xffffb030u : 0xffe83848u);
    screen.rectangle(bar_x,
                     energy_y,
                     bar_width * static_cast<int>(score.energy) / 100,
                     std::max(4, screen.height() / 180), energy_color);
    drawMenuText(screen, "LIFE", bar_x + bar_width + screen.width() / 96,
                 energy_y - label_scale * 7 - screen.height() / 180,
                 label_scale, label);

    screen.rectangle(bar_x, bar_y, bar_width, bar_height, 0xff202838u);
    if (duration_ms > 0) {
        const unsigned int clamped_time = std::min(elapsed_ms, duration_ms);
        const int progress_width = static_cast<int>(
            static_cast<unsigned long long>(bar_width) * clamped_time /
            duration_ms);
        screen.rectangle(bar_x, bar_y, progress_width, bar_height,
                         0xff40b8ffu);
        const unsigned int remaining_ms = duration_ms - clamped_time;
        drawMenuText(screen, "LEFT", screen.width() / 12,
                     bar_y - screen.height() / 48, label_scale, label);
        drawTime(screen, remaining_ms, screen.width() / 6,
                 bar_y - screen.height() / 48, label_scale,
                 0xfff0f0f0u);
    }
}

void drawLevelLibrary(movebeat::ps3::Framebuffer& screen,
                      const std::vector<LevelChoice>& levels,
                      std::size_t selected, std::size_t chart_index) {
    const int center_x = screen.width() / 2;
    const int card_x = center_x - movebeat::ps3::LibraryCard::WIDTH / 2;
    const int center_y = screen.height() / 2 - 55;
    drawMenuText(screen, "TRACKS", center_x - 72, 46, 4, 0xffd9e7ffu);
    if (levels.empty()) {
        drawMenuText(screen, "NO TRACKS", center_x - 90, center_y, 3,
                     0xffffa0a0u);
        return;
    }
    const int counter_x = center_x + 210;
    drawNumber(screen, static_cast<unsigned int>(selected + 1),
               counter_x - 22, 54, 4, 0xffd9e7ffu);
    screen.line(counter_x - 5, 77, counter_x + 5, 52, 0xff7d92b2u, 2);
    drawNumber(screen, static_cast<unsigned int>(levels.size()),
               counter_x + 22, 54, 4, 0xffd9e7ffu);
    const int offsets[] = {-1, 0, 1};
    for (int slot = 0; slot < 3; ++slot) {
        if (levels.size() == 1 && offsets[slot] != 0) continue;
        if (levels.size() == 2 && offsets[slot] < 0) continue;
        const std::size_t index = offsets[slot] < 0
            ? (selected == 0 ? levels.size() - 1 : selected - 1)
            : (selected + static_cast<std::size_t>(offsets[slot])) % levels.size();
        const int y = center_y + offsets[slot] * 116;
        const bool active = offsets[slot] == 0;
        screen.rectangle(card_x - 5, y - 5, 522, 106,
                         active ? 0xff54d8fcu : 0xff303950u);
        if (levels[index].card.ready())
            levels[index].card.draw(screen, card_x, y);
        else {
            screen.rectangle(card_x, y, 512, 96, 0xff121a2eu);
            drawMenuText(screen, "TRACK", card_x + 115, y + 31, 3,
                         0xffd9e7ffu);
        }
    }
    drawMenuText(screen, "TRI PREV  X NEXT  MOVE T SELECT", center_x - 270,
                 screen.height() - 108, 2, 0xff9cb3d3u);
    drawMenuText(screen, "SQ PREV  O NEXT DIFF", center_x - 228,
                 screen.height() - 82, 2, 0xff9cb3d3u);
    drawMenuText(screen, "START CALIBRATE", center_x - 168,
                 screen.height() - 56, 2, 0xff9cb3d3u);
    drawMenuText(screen, "SELECT SWAP HANDS", center_x - 183,
                 screen.height() - 30, 2, 0xff9cb3d3u);
    if (!levels[selected].manifest.charts.empty()) {
        const int count = static_cast<int>(levels[selected].manifest.charts.size());
        for (int index = 0; index < count && index < 20; ++index)
            screen.rectangle(center_x + (index - count / 2) * 18,
                             center_y + 102, 11, 7,
                             static_cast<std::size_t>(index) == chart_index
                                 ? 0xfff6e79au : 0xff4a5574u);
        if (chart_index < levels[selected].manifest.charts.size()) {
            const movebeat::LevelChart& chart =
                levels[selected].manifest.charts[chart_index];
            const int label_x = card_x + movebeat::ps3::LibraryCard::WIDTH + 22;
            drawMenuText(screen, "DIFFICULTY", label_x, center_y + 7,
                         2, 0xff96a9ccu);
            drawMenuText(screen, menuLabel(chart.characteristic).c_str(),
                         label_x, center_y + 36, 2, 0xffb9c9e5u);
            drawMenuText(screen, menuLabel(chart.difficulty).c_str(),
                         label_x, center_y + 61, 3, 0xfff6e79au);
        }
    }
}

void drawMovePointer(movebeat::ps3::Framebuffer& screen,
                     const movebeat::ps3::ScreenPointer& pointer,
                     u32 color) {
    if (!pointer.valid) return;
    screen.circle(pointer.x, pointer.y, 11, color);
    screen.circle(pointer.x, pointer.y, 7, 0xff080b16u);
    screen.circle(pointer.x, pointer.y, 2, color);
    screen.line(pointer.x - 16, pointer.y, pointer.x - 10, pointer.y,
                color, 2);
    screen.line(pointer.x + 10, pointer.y, pointer.x + 16, pointer.y,
                color, 2);
    screen.line(pointer.x, pointer.y - 16, pointer.x, pointer.y - 10,
                color, 2);
    screen.line(pointer.x, pointer.y + 10, pointer.x, pointer.y + 16,
                color, 2);
}

void drawDigit(movebeat::ps3::Framebuffer& screen, unsigned int digit,
               int x, int y, int scale, u32 color) {
    static const unsigned char rows[10][5] = {
        { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 },
        { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 },
        { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 },
        { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 },
        { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 }
    };
    if (digit > 9) return;
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (rows[digit][row] & (1u << (2 - column))) {
                screen.rectangle(x + column * scale, y + row * scale,
                                 scale, scale, color);
            }
        }
    }
}

void drawNumber(movebeat::ps3::Framebuffer& screen, unsigned int value,
                int center_x, int y, int scale, u32 color) {
    char text[16];
    snprintf(text, sizeof(text), "%u", value);
    int length = 0;
    while (text[length]) ++length;
    const int advance = scale * 4;
    int x = center_x - (length * advance - scale) / 2;
    for (int index = 0; index < length; ++index) {
        drawDigit(screen, static_cast<unsigned int>(text[index] - '0'),
                  x, y, scale, color);
        x += advance;
    }
}

void drawHexNumber(movebeat::ps3::Framebuffer& screen, unsigned int value,
                   int center_x, int y, int scale, u32 color) {
    static const unsigned char rows[16][5] = {
        { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 },
        { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 },
        { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 },
        { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 },
        { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
        { 7, 5, 7, 5, 5 }, { 6, 5, 6, 5, 6 },
        { 7, 4, 4, 4, 7 }, { 6, 5, 5, 5, 6 },
        { 7, 4, 7, 4, 7 }, { 7, 4, 7, 4, 4 }
    };
    const int digits = 8;
    const int advance = scale * 4;
    int x = center_x - (digits * advance - scale) / 2;
    for (int index = digits - 1; index >= 0; --index) {
        const unsigned int digit = (value >> (index * 4)) & 0xfu;
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 3; ++column) {
                if (rows[digit][row] & (1u << (2 - column))) {
                    screen.rectangle(x + column * scale, y + row * scale,
                                     scale, scale, color);
                }
            }
        }
        x += advance;
    }
}

void writeStartupFailure(movebeat::ps3::GemRuntime::InitStage stage, int error) {
    FILE* output = 0;
#if defined(_MSC_VER)
    if (fopen_s(&output, kStartupReportPath, "w") != 0) output = 0;
#else
    output = fopen(kStartupReportPath, "w");
#endif
    if (!output) return;
    fprintf(output, "movebeat_startup_report=1\nstage=%u\nerror=0x%08X\n",
            static_cast<unsigned int>(stage), static_cast<unsigned int>(error));
    fclose(output);
}

void drawResults(movebeat::ps3::Framebuffer& screen,
                 const movebeat::ScoreState& score,
                 const movebeat::HighScore& best) {
    const int large = screen.height() / 42 + 2;
    const int small = screen.height() / 70 + 2;
    const u32 score_color = score.failed ? 0xffff5060u : 0xfff0d050u;
    drawNumber(screen, score.score, screen.width() / 2,
               screen.height() / 4, large, score_color);
    drawNumber(screen, best.score, screen.width() / 2,
               screen.height() / 10, small, 0xffa080ffu);
    drawNumber(screen, score.hits, screen.width() / 3,
               screen.height() * 3 / 5, small, 0xff40c8ffu);
    drawNumber(screen, score.misses, screen.width() * 2 / 3,
               screen.height() * 3 / 5, small, 0xffff5060u);
    const int underline_y = screen.height() * 3 / 5 + small * 6;
    screen.rectangle(screen.width() / 4, underline_y,
                     screen.width() / 6, small / 2 + 1, 0xff40c8ffu);
    screen.rectangle(screen.width() * 7 / 12, underline_y,
                     screen.width() / 6, small / 2 + 1, 0xffff5060u);
}

void drawAudioError(movebeat::ps3::Framebuffer& screen, int error) {
    const int center_x = screen.width() / 2;
    const int center_y = screen.height() / 2;
    screen.rectangle(center_x - 330, center_y - 120, 660, 245, 0xff411018u);
    screen.rectangle(center_x - 325, center_y - 115, 650, 235, 0xff200b11u);
    drawMenuText(screen, "AUDIO ERROR", center_x - 155, center_y - 84,
                 4, 0xffff6470u);
    drawHexNumber(screen, static_cast<unsigned int>(error), center_x,
                  center_y - 8, screen.height() / 75 + 2, 0xffffd070u);
    drawMenuText(screen, "TRI BACK TO TRACKS", center_x - 187,
                 center_y + 76, 2, 0xffd9e7ffu);
}

void drawTimingOffset(movebeat::ps3::Framebuffer& screen, int offset_ms) {
    const unsigned int magnitude = static_cast<unsigned int>(
        offset_ms < 0 ? -offset_ms : offset_ms);
    const u32 color = offset_ms > 0 ? 0xff50e080u :
        (offset_ms < 0 ? 0xffff5060u : 0xffe0e0e8u);
    drawNumber(screen, magnitude, screen.width() / 2,
               screen.height() * 6 / 7, screen.height() / 90 + 2, color);
}

void drawBladeLength(movebeat::ps3::Framebuffer& screen, int blade_length_cm) {
    drawNumber(screen, static_cast<unsigned int>(blade_length_cm),
               screen.width() / 2, screen.height() * 6 / 7,
               screen.height() / 90 + 2, 0xff50d8f0u);
}

} // namespace

int main(int, const char**) {
    const unsigned long long process_start_us = nowMicroseconds();
    movebeat::ps3::RunStatus diagnostic;
    if (sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, systemEvent, 0) != 0) {
        printf("Could not register SYSUTIL callback\n");
        diagnostic.state = "fatal";
        diagnostic.phase = "sysutil_callback";
        diagnostic.error_stage = 101;
        movebeat::ps3::writeRunStatus(kRunStatusPath, diagnostic);
        return 1;
    }
    movebeat::ps3::Framebuffer screen;
    if (!screen.initialize()) {
        printf("RSX framebuffer initialization failed\n");
        diagnostic.state = "fatal";
        diagnostic.phase = "framebuffer";
        diagnostic.error_stage = 102;
        movebeat::ps3::writeRunStatus(kRunStatusPath, diagnostic);
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
        return 2;
    }
    printf("MoveBeat PS3 0.1\n");
    diagnostic.phase = "framebuffer_ready";
    movebeat::ps3::writeRunStatus(kRunStatusPath, diagnostic);

    movebeat::ps3::GemRuntime gem;
    const EmulatorDemoOptions demo_options = readEmulatorDemoOptions();
    const bool demo_mode = demo_options.enabled;
    diagnostic.phase = "gem_initializing";
    movebeat::ps3::writeRunStatus(kRunStatusPath, diagnostic);
    if (!demo_mode && !gem.initialize()) {
        const movebeat::ps3::GemRuntime::InitStage stage = gem.lastStage();
        const int error = gem.lastError();
        printf("Camera/libgem initialization failed at stage %u: %08X\n",
               static_cast<unsigned int>(stage), static_cast<unsigned int>(error));
        writeStartupFailure(stage, error);
        diagnostic.state = "fatal";
        diagnostic.phase = "gem_initialization";
        diagnostic.error_stage = static_cast<unsigned int>(stage);
        diagnostic.error_code = static_cast<unsigned int>(error);
        movebeat::ps3::writeRunStatus(kRunStatusPath, diagnostic);
        for (int frame = 0; frame < 600 && g_running; ++frame) {
            screen.beginFrame(0xff300008u);
            screen.rectangle(screen.width() / 7, screen.height() / 4,
                             screen.width() * 5 / 7, screen.height() * 11 / 20,
                             0xffb02030u);
            const int label_scale = std::max(2, screen.height() / 240);
            drawCenteredMenuText(screen, "MOVE START FAILED",
                                 screen.height() * 28 / 100, label_scale,
                                 0xffffffffu);
            drawCenteredMenuText(screen, startupFailureHint(stage, error),
                                 screen.height() * 35 / 100, label_scale,
                                 0xffffd070u);
            drawCenteredMenuText(screen, "STAGE",
                                 screen.height() * 44 / 100, 2,
                                 0xffffffffu);
            drawNumber(screen, static_cast<unsigned int>(stage),
                       screen.width() / 2, screen.height() * 48 / 100,
                       screen.height() / 45, 0xffffffffu);
            drawCenteredMenuText(screen, "ERROR CODE",
                                 screen.height() * 62 / 100, 2,
                                 0xffffffffu);
            drawHexNumber(screen, static_cast<unsigned int>(error),
                          screen.width() / 2, screen.height() * 66 / 100,
                          screen.height() / 55, 0xffffd070u);
            if (frame == 0) screen.savePpm(kFirstFramePath);
            screen.present();
            sysUtilCheckCallback();
        }
        gem.shutdown();
        screen.shutdown();
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
        return 3;
    }

    std::vector<LevelChoice> level_choices = discoverLevels();
    movebeat::Chart selected_chart = tutorialChart();
    movebeat::LevelManifest selected_level;
    std::string level_error;
    std::string level_directory;
    bool imported_level = false;
    std::size_t selected_level_index = 0;
    std::size_t selected_chart_index = 0;
    if (demo_mode && !demo_options.level_id.empty()) {
        bool found = false;
        for (std::size_t index = 0; index < level_choices.size(); ++index) {
            const std::string name = level_choices[index].directory.substr(
                std::string(kLevelRoot).size() + 1);
            if (name == demo_options.level_id) {
                selected_level_index = index;
                found = true;
                break;
            }
        }
        if (!found) {
            printf("Demo level ID not found: %s\n", demo_options.level_id.c_str());
            diagnostic.state = "fatal";
            diagnostic.phase = "demo_level_missing";
            diagnostic.error_stage = 104;
            diagnostic.level_count = static_cast<int>(level_choices.size());
            movebeat::ps3::writeRunStatus(kRunStatusPath, diagnostic);
            screen.shutdown();
            sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
            return 4;
        }
    }
    if (!level_choices.empty()) {
        updateLibraryCards(level_choices, selected_level_index);
        recordLibraryCardMemory(diagnostic, level_choices);
        level_directory = level_choices[selected_level_index].directory;
        selected_level = level_choices[selected_level_index].manifest;
    }
    if (!level_directory.empty() &&
        demo_mode && demo_options.chart_index < selected_level.charts.size())
        selected_chart_index = demo_options.chart_index;
    else if (demo_mode && demo_options.chart_index != 0)
        printf("Demo chart index %u is unavailable; using chart 0\n",
               demo_options.chart_index);
    if (!level_directory.empty() &&
        movebeat::loadLevelChart(level_directory, selected_level,
                                 selected_chart_index,
                                 selected_chart, level_error)) {
        imported_level = true;
        printf("Loaded level: %s (%s)\n", selected_level.title.c_str(),
               selected_level.charts[selected_chart_index].difficulty.c_str());
    } else {
        printf("Using built-in tutorial: %s\n", level_error.c_str());
    }
    movebeat::GameConfig game_config;
    // Match vertical note placement to the pitched spectator camera. The
    // upper row remains at its authored height; the middle/lower rows use this
    // slope to keep the center level and the lower row below the sightline.
    game_config.row_pitch_per_depth =
        -(movebeat::ps3::kProjectionEyeHeight +
          movebeat::ps3::kProjectionCameraHeight) /
        movebeat::ps3::kProjectionDepth;
    movebeat::Game game(game_config);
    const auto reloadSelectedChart = [&]() {
        movebeat::GameConfig chart_config = game_config;
        if (imported_level && selected_chart_index < selected_level.charts.size()) {
            const movebeat::LevelChart& chart_metadata =
                selected_level.charts[selected_chart_index];
            if (chart_metadata.note_jump_speed > 0.0f &&
                !movebeat::configureNoteJump(
                    chart_config, selected_level.bpm,
                    chart_metadata.note_jump_speed,
                    chart_metadata.start_beat_offset)) {
                printf("Unsafe note jump metadata for %s/%s; using default\n",
                       chart_metadata.characteristic.c_str(),
                       chart_metadata.difficulty.c_str());
            }
        }
        game.setConfig(chart_config);
        game.load(selected_chart);
    };
    reloadSelectedChart();
    unsigned int chart_duration_ms = estimateChartDurationMs(selected_chart);
    movebeat::ps3::CutFeedback cut_feedback;
    movebeat::ps3::AudioPlayer audio;
    bool audio_clock = false;
    int audio_start_error = 0;
    movebeat::HighScore best_score;
    movebeat::Settings settings;
    std::string settings_error;
    if (!movebeat::loadSettingsFile(kSettingsPath, settings, settings_error)) {
        printf("Settings read failed: %s; using zero offset\n",
               settings_error.c_str());
        settings = movebeat::Settings();
    }
    std::string audio_path = imported_level
        ? level_directory + "/" + selected_level.audio
        : std::string();
    unsigned long long level_start_us = nowMicroseconds();
    RunState run_state = RUN_SELECT;
    movebeat::Vec3 play_origin;
    bool hand_slots_swapped = settings.invert_hands;
    bool hand_assignment_established = false;
    movebeat::PoseCalibration pose_calibration;
    bool calibrating = false;
    movebeat::ps3::MenuButtonEdges menu_buttons;
    movebeat::ps3::MenuNavigationRepeat menu_navigation_repeat;
    int left_rumble_frames = 0;
    int right_rumble_frames = 0;
    bool paused = false;
    bool tracking_paused = false;
    bool tracking_recalibrating = false;
    movebeat::ps3::TrackingPauseGate tracking_gate;
    movebeat::PoseCalibration tracking_calibration;
    unsigned long long pause_started_us = 0;
    unsigned int diagnostic_frame = 0;
    movebeat::ps3::FrameTimingWindow frame_work_timing;
    movebeat::ps3::FrameTimingWindow frame_wait_timing;
    movebeat::ps3::FrameTimingWindow frame_active_timing;
    movebeat::ps3::FrameTimingWindow frame_render_timing;
    movebeat::ps3::FrameTimingWindow frame_clear_timing;
    movebeat::ps3::FrameTimingWindow frame_scene_timing;
    movebeat::ps3::FrameTimingWindow frame_gpu_notes_timing;
    const char* previous_diagnostic_phase = "";
    bool first_frame_saved = false;
    bool demo_frame_saved = false;
    bool demo_scored_frame_saved = false;
    unsigned int demo_cycles = 0;
    unsigned long long demo_loss_started_us = 0;
    bool demo_loss_finished = false;

    if (demo_mode) {
        // Explicit, local-only RPCS3 preview. Never infer this from a missing Eye:
        // on hardware, a camera failure must remain visible to the player.
        run_state = RUN_PLAY;
        game.reset();
        cut_feedback.clear();
        level_start_us = nowMicroseconds();
        if (imported_level) {
            audio_clock = audio.start(audio_path);
            if (!audio_clock) {
                audio_start_error = audio.lastError();
                run_state = RUN_AUDIO_ERROR;
                diagnostic.audio_error_code =
                    static_cast<unsigned int>(audio.lastError());
                printf("Demo audio start failed: %08X\n", audio.lastError());
            }
        }
        printf("Emulator demo enabled; Move poses are synthetic\n");
    }

    diagnostic.state = "running";
    diagnostic.phase = run_state == RUN_AUDIO_ERROR ? "audio_error" :
        (demo_mode ? "demo_play" : "select");
    diagnostic.level_count = static_cast<int>(level_choices.size());
    diagnostic.imported_level = imported_level ? 1 : 0;
    diagnostic.level_id = imported_level ? selected_level.source_key : "builtin_tutorial";
    diagnostic.chart_difficulty = imported_level && !selected_level.charts.empty()
        ? selected_level.charts[selected_chart_index].difficulty : "Easy";
    movebeat::ps3::writeRunStatus(kRunStatusPath, diagnostic);

    const auto synchronizePause = [&]() {
        const bool requested = g_pause_requested || tracking_paused;
        if (requested == paused) return;
        const unsigned long long now_us = nowMicroseconds();
        paused = requested;
        if (paused) {
            pause_started_us = now_us;
            if (audio_clock && !audio.pause()) {
                audio_start_error = audio.lastError();
                diagnostic.audio_error_code =
                    static_cast<unsigned int>(audio_start_error);
                printf("Audio pause failed: %08X\n", audio_start_error);
                audio.shutdown();
                audio_clock = false;
                run_state = RUN_AUDIO_ERROR;
                tracking_paused = false;
                tracking_recalibrating = false;
            }
            left_rumble_frames = 0;
            right_rumble_frames = 0;
        } else {
            level_start_us += now_us - pause_started_us;
            if (audio_clock && !audio.resume()) {
                audio_start_error = audio.lastError();
                diagnostic.audio_error_code =
                    static_cast<unsigned int>(audio_start_error);
                printf("Audio resume failed: %08X\n", audio_start_error);
                audio.shutdown();
                audio_clock = false;
                run_state = RUN_AUDIO_ERROR;
                tracking_paused = false;
                tracking_recalibrating = false;
            }
        }
    };

    while (g_running) {
        const unsigned long long frame_work_start_us = nowMicroseconds();
        synchronizePause();
        if (run_state == RUN_PLAY && audio_clock && audio.lastError() != 0) {
            audio_start_error = audio.lastError();
            diagnostic.audio_error_code =
                static_cast<unsigned int>(audio_start_error);
            printf("Audio stream failed: %08X\n", audio_start_error);
            audio.shutdown();
            audio_clock = false;
            run_state = RUN_AUDIO_ERROR;
            tracking_paused = false;
            tracking_recalibrating = false;
        }
        movebeat::ps3::MoveFrame moves;
        bool gem_update_ok = false;
        if (demo_mode) {
            const unsigned int elapsed_ms = movebeat::ps3::unpausedElapsedMs(
                nowMicroseconds(), level_start_us, pause_started_us, paused);
            moves = movebeat::ps3::scriptedDemoFrame(
                game, adjustedTime(audio_clock ? audio.timeMs() : elapsed_ms,
                                   settings.timing_offset_ms),
                demo_options.automatic_cuts);
            if (demo_options.tracking_loss_test && !demo_loss_finished) {
                if (!demo_loss_started_us && elapsed_ms >= 1200u)
                    demo_loss_started_us = nowMicroseconds();
                if (demo_loss_started_us) {
                    const unsigned int loss_elapsed_ms = static_cast<unsigned int>(
                        (nowMicroseconds() - demo_loss_started_us) / 1000ull);
                    if (loss_elapsed_ms < 450u) {
                        moves.left.tracked = false;
                    } else if (tracking_paused || loss_elapsed_ms < 1800u) {
                        moves.left = movebeat::SaberPose(
                            movebeat::Vec3(-0.35f, -0.7f, -0.3f),
                            movebeat::Vec3(-0.35f, 0.0f, 0.0f), true);
                        moves.right = movebeat::SaberPose(
                            movebeat::Vec3(0.35f, -0.7f, -0.3f),
                            movebeat::Vec3(0.35f, 0.0f, 0.0f), true);
                        if (loss_elapsed_ms >= 500u && loss_elapsed_ms < 600u)
                            moves.left_buttons = kButtonStart;
                    } else if (diagnostic.tracking_recovery_count > 0u) {
                        demo_loss_finished = true;
                        game.invalidateInput();
                    }
                }
            }
        } else {
            gem_update_ok = gem.update(
                moves, static_cast<float>(settings.blade_length_cm) * 0.01f,
                run_state == RUN_SELECT && !calibrating && !paused);
        }
        const movebeat::ps3::MenuButtonSample buttons = menu_buttons.update(
            moves, demo_mode ? 3u : gem.lastButtonReadyMask());
        movebeat::ps3::ScreenPointer move_pointers[2];
        movebeat::ps3::LibraryPointerTarget move_targets[2] = {
            movebeat::ps3::LIBRARY_POINTER_NONE,
            movebeat::ps3::LIBRARY_POINTER_NONE
        };
        if (run_state == RUN_SELECT && !calibrating && !paused) {
            for (unsigned int slot = 0; slot < 2; ++slot) {
                move_pointers[slot] = movebeat::ps3::moveImageScreenPoint(
                    moves.image[slot], screen.width(), screen.height());
                if (move_pointers[slot].valid)
                    move_targets[slot] = movebeat::ps3::hitTestLibraryPointer(
                        move_pointers[slot].x, move_pointers[slot].y,
                        screen.width(), screen.height(), level_choices.size());
            }
        }
        const unsigned short left_t_press = static_cast<unsigned short>(
            buttons.left_pressed & movebeat::ps3::GEM_BUTTON_T);
        const unsigned short right_t_press = static_cast<unsigned short>(
            buttons.right_pressed & movebeat::ps3::GEM_BUTTON_T);
        const unsigned int pointer_t_presses =
            (left_t_press ? 1u : 0u) + (right_t_press ? 1u : 0u);
        movebeat::ps3::LibraryPointerTarget activated_pointer =
            movebeat::ps3::LIBRARY_POINTER_NONE;
        if (pointer_t_presses == 1u) {
            activated_pointer = left_t_press ? move_targets[0] : move_targets[1];
        }
        unsigned short pointer_navigation = 0;
        bool pointer_start = false;
        if (activated_pointer == movebeat::ps3::LIBRARY_POINTER_PREVIOUS)
            pointer_navigation = kButtonTriangle;
        else if (activated_pointer == movebeat::ps3::LIBRARY_POINTER_NEXT)
            pointer_navigation = kButtonCross;
        else if (activated_pointer == movebeat::ps3::LIBRARY_POINTER_CURRENT)
            pointer_start = true;
        unsigned short repeated_navigation = 0;
        if (run_state == RUN_SELECT && !calibrating && !paused) {
            repeated_navigation = menu_navigation_repeat.update(
                buttons.held, buttons.pressed,
                static_cast<unsigned int>(nowMicroseconds() / 1000ull));
        } else {
            menu_navigation_repeat.reset();
        }
        const bool calibration_hands_separated =
            movebeat::ps3::handsSeparatedForCalibration(moves.left, moves.right);
        const unsigned short selection_pressed = static_cast<unsigned short>(
            buttons.pressed & (kButtonCircle | kButtonSquare));
        const unsigned short level_pressed = static_cast<unsigned short>(
            (buttons.pressed | repeated_navigation | pointer_navigation) &
            (kButtonTriangle | kButtonCross));
        const bool hand_toggle = run_state == RUN_SELECT && !calibrating &&
            !paused && (buttons.pressed & kButtonSelect) != 0;
        if (hand_toggle) {
            settings.invert_hands = !settings.invert_hands;
            hand_slots_swapped = !hand_slots_swapped;
            if (hand_assignment_established)
                gem.setHandSlotsSwapped(hand_slots_swapped);
            if (!movebeat::saveSettingsFile(kSettingsPath, settings, settings_error))
                printf("Settings write failed: %s\n", settings_error.c_str());
            printf("Manual hand inversion: %s\n",
                   settings.invert_hands ? "on" : "off");
        }
        const bool cancel_calibration = calibrating &&
            (level_pressed & kButtonTriangle) != 0;
        if (cancel_calibration) {
            calibrating = false;
            pose_calibration.reset();
            printf("Pose calibration cancelled\n");
        }
        const bool left_trigger_down = buttons.left_trigger >= 128u;
        const bool right_trigger_down = buttons.right_trigger >= 128u;
        const bool blade_modifier = left_trigger_down && right_trigger_down;
        const bool timing_modifier = (left_trigger_down || right_trigger_down) &&
                                     !blade_modifier;
        if (run_state == RUN_SELECT && !calibrating && !cancel_calibration &&
            !hand_toggle &&
            level_pressed && level_choices.size() > 1) {
            std::size_t candidate = selected_level_index;
            if (level_pressed & kButtonCross) {
                candidate = (candidate + 1) % level_choices.size();
            } else if (level_pressed & kButtonTriangle) {
                candidate = candidate == 0 ? level_choices.size() - 1 : candidate - 1;
            }
            movebeat::Chart replacement;
            if (movebeat::loadLevelChart(level_choices[candidate].directory,
                                         level_choices[candidate].manifest, 0,
                                         replacement, level_error)) {
                selected_level_index = candidate;
                updateLibraryCards(level_choices, selected_level_index);
                recordLibraryCardMemory(diagnostic, level_choices);
                selected_chart_index = 0;
                level_directory = level_choices[candidate].directory;
                selected_level = level_choices[candidate].manifest;
                selected_chart = replacement;
                chart_duration_ms = estimateChartDurationMs(selected_chart);
                imported_level = true;
                audio_start_error = 0;
                diagnostic.audio_error_code = 0;
                audio_path = level_directory + "/" + selected_level.audio;
                reloadSelectedChart();
                printf("Selected level: %s (%s)\n", selected_level.title.c_str(),
                       selected_level.charts[0].difficulty.c_str());
            } else {
                printf("Level selection failed: %s\n", level_error.c_str());
            }
        }
        if (run_state == RUN_SELECT && !calibrating && !cancel_calibration &&
            !hand_toggle &&
            blade_modifier && selection_pressed) {
            if (selection_pressed & kButtonCircle)
                settings.blade_length_cm += 5;
            else if (selection_pressed & kButtonSquare)
                settings.blade_length_cm -= 5;
            settings.blade_length_cm = clamp(
                settings.blade_length_cm,
                movebeat::Settings::MIN_BLADE_LENGTH_CM,
                movebeat::Settings::MAX_BLADE_LENGTH_CM);
            if (!movebeat::saveSettingsFile(kSettingsPath, settings, settings_error))
                printf("Settings write failed: %s\n", settings_error.c_str());
            printf("Blade length: %d cm\n", settings.blade_length_cm);
        } else if (run_state == RUN_SELECT && !calibrating && !cancel_calibration &&
                   !hand_toggle &&
                   timing_modifier && selection_pressed) {
            if (selection_pressed & kButtonCircle)
                settings.timing_offset_ms += 10;
            else if (selection_pressed & kButtonSquare)
                settings.timing_offset_ms -= 10;
            settings.timing_offset_ms = clamp(settings.timing_offset_ms, -500, 500);
            if (!movebeat::saveSettingsFile(kSettingsPath, settings, settings_error))
                printf("Settings write failed: %s\n", settings_error.c_str());
            printf("Timing offset: %+d ms\n", settings.timing_offset_ms);
        } else if (run_state == RUN_SELECT && !calibrating && !cancel_calibration &&
            !hand_toggle &&
            !blade_modifier && !timing_modifier &&
            imported_level && selection_pressed &&
            !selected_level.charts.empty()) {
            if (selection_pressed & kButtonCircle) {
                selected_chart_index =
                    (selected_chart_index + 1) % selected_level.charts.size();
            } else if (selection_pressed & kButtonSquare) {
                selected_chart_index = selected_chart_index == 0
                    ? selected_level.charts.size() - 1 : selected_chart_index - 1;
            }
            movebeat::Chart replacement;
            if (movebeat::loadLevelChart(level_directory, selected_level,
                                         selected_chart_index, replacement,
                                         level_error)) {
                selected_chart = replacement;
                chart_duration_ms = estimateChartDurationMs(selected_chart);
                reloadSelectedChart();
                printf("Selected chart: %s/%s\n",
                       selected_level.charts[selected_chart_index].characteristic.c_str(),
                       selected_level.charts[selected_chart_index].difficulty.c_str());
            } else {
                printf("Chart selection failed: %s\n", level_error.c_str());
            }
        }
        if (run_state == RUN_AUDIO_ERROR && (level_pressed & kButtonTriangle)) {
            run_state = RUN_SELECT;
            audio_start_error = 0;
            diagnostic.audio_error_code = 0;
        }
        if (run_state == RUN_RESULTS && (level_pressed & kButtonTriangle)) {
            run_state = RUN_SELECT;
            game.reset();
            cut_feedback.clear();
            printf("Returned to level selection\n");
        }
        const bool start_pressed = (buttons.pressed & kButtonStart) != 0 ||
                                   pointer_start;
        const bool both_moves_tracked = moves.left.tracked && moves.right.tracked;
        if (run_state == RUN_PLAY && (!demo_mode || demo_options.tracking_loss_test) && !paused &&
            tracking_gate.observe(
                static_cast<unsigned int>(nowMicroseconds() / 1000ull),
                both_moves_tracked)) {
            tracking_paused = true;
            tracking_recalibrating = false;
            tracking_calibration.reset();
            game.invalidateInput();
            synchronizePause();
            ++diagnostic.tracking_pause_count;
            diagnostic.tracking_pause_audio_ms = audio_clock ? audio.timeMs() : 0u;
            diagnostic.tracking_pause_score = game.score().score;
            diagnostic.tracking_pause_hits = game.score().hits;
            diagnostic.tracking_pause_misses = game.score().misses;
            printf("Tracking lost; audio and chart paused\n");
        }
        if (run_state == RUN_PLAY && tracking_paused) {
            if (!both_moves_tracked || g_pause_requested) {
                tracking_calibration.reset();
                tracking_recalibrating = false;
            } else {
                if (start_pressed && !tracking_recalibrating) {
                    tracking_calibration.reset();
                    tracking_recalibrating = true;
                    printf("Recalibrating both Move after tracking loss\n");
                }
                if (tracking_recalibrating) {
                    if (!calibration_hands_separated)
                        tracking_calibration.reset();
                    else if (tracking_calibration.sample(
                        static_cast<unsigned int>(nowMicroseconds() / 1000ull),
                        moves.left, moves.right)) {
                        play_origin = tracking_calibration.origin();
                        hand_slots_swapped =
                            movebeat::ps3::shouldSwapHandSlots(
                                moves.left, moves.right) != settings.invert_hands;
                        hand_assignment_established = true;
                        gem.setHandSlotsSwapped(hand_slots_swapped);
                        tracking_recalibrating = false;
                        tracking_paused = false;
                        tracking_gate.reset();
                        game.invalidateInput();
                        diagnostic.tracking_resume_audio_ms =
                            audio_clock ? audio.timeMs() : 0u;
                        diagnostic.tracking_resume_score = game.score().score;
                        diagnostic.tracking_resume_hits = game.score().hits;
                        diagnostic.tracking_resume_misses = game.score().misses;
                        synchronizePause();
                        ++diagnostic.tracking_recovery_count;
                        printf("Tracking recovered; level resumed\n");
                    }
                }
            }
        }
        bool calibrated_start = false;
        if (calibrating && !paused) {
            if (calibration_hands_separated)
                calibrated_start = pose_calibration.sample(
                    static_cast<unsigned int>(nowMicroseconds() / 1000ull),
                    moves.left, moves.right);
            else
                pose_calibration.reset();
            if (calibrated_start) {
                play_origin = pose_calibration.origin();
                hand_slots_swapped = movebeat::ps3::shouldSwapHandSlots(
                    moves.left, moves.right) != settings.invert_hands;
                hand_assignment_established = true;
                gem.setHandSlotsSwapped(hand_slots_swapped);
                calibrating = false;
                printf("Starting pose assigned GEM slot %u to left hand\n",
                       movebeat::ps3::physicalSlotForHand(
                           0u, hand_slots_swapped));
            }
        }
        if (paused) pose_calibration.reset();
        if (start_pressed && run_state == RUN_SELECT && !cancel_calibration &&
            !hand_toggle &&
            blade_modifier) {
            calibrating = false;
            if (gem.captureActive()) {
                gem.stopCapture();
                printf("GEM capture stopped: %s\n", kCapturePath);
            } else if (gem.startCapture(kCapturePath)) {
                printf("GEM capture started (30 second limit): %s\n", kCapturePath);
            } else {
                printf("GEM capture could not open: %s\n", kCapturePath);
            }
        } else if (start_pressed && run_state == RUN_SELECT && !paused &&
                   !cancel_calibration && !hand_toggle) {
            pose_calibration.reset();
            calibrating = true;
            printf("Hold both Move steady in a comfortable forward pose for one second\n");
        } else if (calibrated_start ||
                   (start_pressed && run_state == RUN_RESULTS && !paused &&
                    moves.left.tracked && moves.right.tracked)) {
            if (run_state == RUN_SELECT || run_state == RUN_RESULTS) {
                run_state = RUN_PLAY;
                tracking_paused = false;
                tracking_recalibrating = false;
                tracking_gate.reset();
                game.reset();
                cut_feedback.clear();
                best_score = movebeat::HighScore();
                level_start_us = nowMicroseconds();
                audio_clock = false;
                if (imported_level) {
                    audio_clock = audio.start(audio_path);
                    if (!audio_clock) {
                        audio_start_error = audio.lastError();
                        run_state = RUN_AUDIO_ERROR;
                        diagnostic.audio_error_code =
                            static_cast<unsigned int>(audio_start_error);
                        printf("Audio start failed: %08X\n", audio.lastError());
                    } else {
                        audio_start_error = 0;
                        diagnostic.audio_error_code = 0;
                    }
                }
                if (run_state == RUN_PLAY) printf("Level started\n");
            }
        }
        if (hand_slots_swapped) movebeat::ps3::swapMoveFrameHands(moves);
        if (run_state == RUN_PLAY) {
            moves.left = translated(moves.left, play_origin);
            moves.right = translated(moves.right, play_origin);
        }
        const unsigned int raw_level_time_ms = audio_clock ? audio.timeMs() :
            (run_state == RUN_PLAY
                ? movebeat::ps3::unpausedElapsedMs(
                    nowMicroseconds(), level_start_us, pause_started_us, paused)
                : 0u);
        unsigned int level_time_ms = adjustedTime(raw_level_time_ms,
                                                  settings.timing_offset_ms);
        if (run_state == RUN_PLAY && !paused) {
            game.update(level_time_ms, moves.left, moves.right);
            const movebeat::FrameEvents& events = game.events();
            cut_feedback.add(events, level_time_ms);
            if (events.left_hit) left_rumble_frames = 2;
            if (events.right_hit) right_rumble_frames = 2;
            if (events.miss) {
                left_rumble_frames = events.bomb_hit ? 5 : 3;
                right_rumble_frames = events.bomb_hit ? 5 : 3;
            }
        }
        if (!demo_mode) {
            gem.setRumble(movebeat::ps3::physicalSlotForHand(
                              0u, hand_slots_swapped),
                          left_rumble_frames > 0 ? 90 : 0);
            gem.setRumble(movebeat::ps3::physicalSlotForHand(
                              1u, hand_slots_swapped),
                          right_rumble_frames > 0 ? 90 : 0);
        }
        if (left_rumble_frames > 0) --left_rumble_frames;
        if (right_rumble_frames > 0) --right_rumble_frames;
        const bool audio_tail_finished = !audio_clock || audio.finished() ||
                                         game.score().failed;
        if (run_state == RUN_PLAY && !paused && game.finished(level_time_ms) &&
            audio_tail_finished) {
            if (demo_mode) {
                // Keep the visual preview alive without producing a fake score.
                printf("Demo cycle %u: score=%u hits=%u misses=%u\n",
                       demo_cycles + 1u, game.score().score,
                       game.score().hits, game.score().misses);
                diagnostic.last_cycle_score = game.score().score;
                diagnostic.last_cycle_hits = game.score().hits;
                diagnostic.last_cycle_misses = game.score().misses;
                ++demo_cycles;
                audio.shutdown();
                audio_clock = false;
                game.reset();
                cut_feedback.clear();
                level_start_us = nowMicroseconds();
                if (imported_level) {
                    audio_clock = audio.start(audio_path);
                    if (!audio_clock) {
                        audio_start_error = audio.lastError();
                        run_state = RUN_AUDIO_ERROR;
                        diagnostic.audio_error_code =
                            static_cast<unsigned int>(audio.lastError());
                        printf("Demo audio restart failed: %08X\n",
                               audio.lastError());
                    }
                }
                left_rumble_frames = right_rumble_frames = 0;
            } else {
                audio.shutdown();
                audio_clock = false;
                left_rumble_frames = right_rumble_frames = 0;
                run_state = RUN_RESULTS;
                const std::string level_identity = imported_level
                    ? (selected_level.source_key.empty()
                        ? level_directory : selected_level.source_key)
                    : "builtin_tutorial";
                const std::string chart_identity = imported_level &&
                    selected_chart_index < selected_level.charts.size()
                    ? selected_level.charts[selected_chart_index].file
                    : "tutorial";
                const std::string score_key = movebeat::makeScoreKey(
                    level_identity, chart_identity);
                std::string score_error;
                if (!movebeat::loadBestScoreFile(kScoreLog, score_key,
                                                 best_score, score_error)) {
                    printf("Score read failed: %s\n", score_error.c_str());
                }
                const movebeat::ScoreState& current = game.score();
                if (current.score > best_score.score ||
                    (current.score == best_score.score &&
                     current.max_combo > best_score.max_combo)) {
                    best_score.score = current.score;
                    best_score.max_combo = current.max_combo;
                    if (!movebeat::appendScoreFile(kScoreLog, score_key,
                                                   best_score, score_error)) {
                        printf("Score write failed: %s\n", score_error.c_str());
                    }
                }
                printf("Level finished: score=%u hits=%u misses=%u\n",
                       game.score().score, game.score().hits, game.score().misses);
            }
        }

        const unsigned long long frame_wait_start_us = nowMicroseconds();
        screen.waitForPreviousFlip();
        const unsigned long long frame_render_start_us = nowMicroseconds();
        const unsigned int frame_wait_us = static_cast<unsigned int>(
            frame_render_start_us - frame_wait_start_us);
        frame_wait_timing.add(frame_wait_us);
        screen.beginFrame(game.score().failed ? 0xff200508u : 0xff050510u);
        const unsigned long long frame_clear_end_us = nowMicroseconds();
        frame_clear_timing.add(static_cast<unsigned int>(
            frame_clear_end_us - frame_render_start_us));
        if (run_state == RUN_RESULTS) {
            drawResults(screen, game.score(), best_score);
        } else if (run_state == RUN_SELECT) {
            drawLevelLibrary(screen, level_choices, selected_level_index,
                             selected_chart_index);
            const unsigned int left_slot =
                movebeat::ps3::physicalSlotForHand(0u, hand_slots_swapped);
            drawMovePointer(screen, move_pointers[0],
                left_slot == 0u ? 0xff40b8ffu : 0xffff4058u);
            drawMovePointer(screen, move_pointers[1],
                left_slot == 1u ? 0xff40b8ffu : 0xffff4058u);
        } else if (run_state == RUN_AUDIO_ERROR) {
            drawAudioError(screen, audio_start_error);
        } else {
            drawGrid(screen, game.noteSpawnDepth());
            drawObstacles(screen, game, level_time_ms);
            drawArcs(screen, game, level_time_ms);
            const unsigned long long frame_scene_end_us = nowMicroseconds();
            frame_scene_timing.add(static_cast<unsigned int>(
                frame_scene_end_us - frame_clear_end_us));
            const bool solid_notes_drawn = screen.drawSolidNotes(game, level_time_ms);
            frame_gpu_notes_timing.add(static_cast<unsigned int>(
                nowMicroseconds() - frame_scene_end_us));
            if (solid_notes_drawn) {
                const unsigned int cubes = screen.lastSolidNoteCount();
                if (cubes) {
#if defined(MOVEBEAT_EXPERIMENTAL_RSX_NOTES)
                    ++diagnostic.gpu_note_draw_calls;
                    diagnostic.gpu_note_max_cubes = std::max(
                        diagnostic.gpu_note_max_cubes, cubes);
#else
                    ++diagnostic.software_note_draw_calls;
                    diagnostic.software_note_max_cubes = std::max(
                        diagnostic.software_note_max_cubes, cubes);
#endif
                }
            } else {
                ++diagnostic.gpu_note_fallback_frames;
            }
            drawNotes(screen, game, level_time_ms, !solid_notes_drawn);
            if (!solid_notes_drawn) {
                drawMenuText(screen, "3D RENDER UNAVAILABLE", 24,
                             screen.height() - 54, 2, 0xffffad8bu);
            }
            drawCutFeedback(screen, cut_feedback, level_time_ms);
            drawSaber(screen, moves.left, 0xff70d8ffu, 0xff174870u);
            drawSaber(screen, moves.right, 0xffff6878u, 0xff701727u);
        }
        drawConnection(screen, 0, moves.left_connected, moves.left.tracked, 0xff40b8ffu);
        drawConnection(screen, 1, moves.right_connected, moves.right.tracked, 0xffff4058u);
        if (run_state == RUN_PLAY && tracking_paused) {
            const int width = screen.width() / 2;
            const int x = (screen.width() - width) / 2;
            const int y = screen.height() / 3;
            screen.rectangle(x - 12, y - 20, width + 24, 194, 0xff111b30u);
            const char* message = !both_moves_tracked ? "TRACKING LOST" :
                (!tracking_recalibrating ? "START RECALIBRATE" :
                 (!calibration_hands_separated ? "SEPARATE MOVE HANDS" :
                  "HOLD BOTH MOVE STILL"));
            drawMenuText(screen, message, x + 25, y, 3,
                         both_moves_tracked ? 0xffd9e7ffu : 0xffffad8bu);
            drawMenuText(screen, !both_moves_tracked ? "SHOW BOTH MOVE" :
                         (tracking_recalibrating ? "RETURN TO START POSE" :
                          "PRESS START"), x + 25, y + 50, 2, 0xff9cb3d3u);
            drawMenuText(screen, "HANDS APART - BUTTONS UP - SABERS FORWARD",
                         x + 25, y + 77, 2, 0xff9cb3d3u);
            screen.rectangle(x + 25, y + 108, width - 50, 14, 0xff303b56u);
            if (tracking_recalibrating && calibration_hands_separated) {
                const unsigned int elapsed = std::min(
                    1000u, tracking_calibration.progressMs());
                screen.rectangle(x + 25, y + 108,
                                 (width - 50) * elapsed / 1000, 14,
                                 0xff40e890u);
            }
        }
        if (calibrating) {
            const int width = screen.width() / 2;
            const int x = (screen.width() - width) / 2;
            const int y = screen.height() / 8;
            const bool both_tracked = moves.left.tracked && moves.right.tracked;
            const unsigned int elapsed = std::min(1000u, pose_calibration.progressMs());
            screen.rectangle(x - 12, y - 18, width + 24, 122, 0xff111b30u);
            drawMenuText(screen, !both_tracked ? "TRACK BOTH SPHERES" :
                         (!calibration_hands_separated ? "SEPARATE MOVE HANDS" :
                          "HOLD BOTH MOVE STILL"), x + 12, y, 3,
                         calibration_hands_separated ? 0xffd9e7ffu : 0xffffad8bu);
            drawMenuText(screen, "HANDS APART - BUTTONS UP - SABERS FORWARD",
                         x + 12, y + 27, 2, 0xff9cb3d3u);
            screen.rectangle(x, y + 52, width, 14, 0xff303b56u);
            screen.rectangle(x, y + 52, width * elapsed / 1000, 14,
                             calibration_hands_separated ? 0xff40e890u : 0xffffad8bu);
            drawMenuText(screen, "TRI CANCEL", x + width / 2 - 65,
                         y + 80, 2, 0xff9cb3d3u);
        }
        if (gem.captureActive()) {
            const int capture_size = screen.height() / 42 + 4;
            screen.rectangle(screen.width() - capture_size * 3,
                             screen.height() / 30, capture_size, capture_size,
                             0xffe040d0u);
        }
        if (run_state == RUN_PLAY && imported_level && !selected_level.charts.empty()) {
            const int pip = screen.height() / 70 + 4;
            const int gap = pip / 2;
            const int total = static_cast<int>(selected_level.charts.size()) * (pip + gap);
            const int start_x = screen.width() / 2 - total / 2;
            for (std::size_t index = 0; index < selected_level.charts.size(); ++index) {
                screen.rectangle(start_x + static_cast<int>(index) * (pip + gap),
                                 screen.height() / 30, pip, pip,
                                 index == selected_chart_index
                                     ? 0xfff0f0f0u : 0xff404050u);
            }
        }
        if (run_state == RUN_SELECT) {
            drawMenuText(screen, settings.invert_hands ? "HANDS FLIPPED" :
                         "HANDS AUTO", 24, screen.height() - 30, 2,
                         settings.invert_hands ? 0xffffb068u : 0xff80d8c0u);
            if (blade_modifier)
                drawBladeLength(screen, settings.blade_length_cm);
            else if (timing_modifier)
                drawTimingOffset(screen, settings.timing_offset_ms);
        }
        screen.rectangle(screen.width() - screen.width() / 20, screen.height() / 30,
                         screen.width() / 40, screen.height() / 30,
                         run_state == RUN_PLAY ? 0xff40e070u :
                         (run_state == RUN_RESULTS ? 0xff8060d0u : 0xffffa020u));
        if (run_state == RUN_PLAY) {
            const unsigned int song_duration_ms = audio_clock
                ? audio.durationMs() : chart_duration_ms;
            drawHud(screen, game.score(), level_time_ms, song_duration_ms);
        }
        const unsigned long long frame_work_end_us = nowMicroseconds();
        frame_work_timing.add(static_cast<unsigned int>(
            frame_work_end_us - frame_work_start_us));
        const unsigned int frame_work_us = static_cast<unsigned int>(
            frame_work_end_us - frame_work_start_us);
        frame_active_timing.add(frame_work_us > frame_wait_us
            ? frame_work_us - frame_wait_us : 0u);
        frame_render_timing.add(static_cast<unsigned int>(
            frame_work_end_us - frame_render_start_us));
        if (!first_frame_saved) {
            first_frame_saved = screen.savePpm(kFirstFramePath);
            if (first_frame_saved)
                printf("First rendered frame saved: %s\n", kFirstFramePath);
        }
        bool visible_upper_note = false;
        bool visible_lower_note = false;
        if (demo_mode && !demo_frame_saved && run_state == RUN_PLAY) {
            for (std::size_t index = 0; index < game.noteCount(); ++index) {
                if (!game.notePending(index)) continue;
                const movebeat::Note& note = game.note(index);
                const movebeat::Vec3 position = game.notePosition(note, level_time_ms);
                if (position.z < -0.8f || position.z > game.noteSpawnDepth())
                    continue;
                if (note.row >= 2) visible_upper_note = true;
                else visible_lower_note = true;
            }
        }
        // Prefer a frame that actually demonstrates the vertical spread, not
        // an arbitrary early frame before any chart notes enter the playfield.
        if (demo_mode && !demo_frame_saved && run_state == RUN_PLAY &&
            raw_level_time_ms >= 1200u && visible_upper_note &&
            visible_lower_note) {
            demo_frame_saved = screen.savePpm(kDemoFramePath);
            if (demo_frame_saved)
                printf("Emulator perspective frame saved: %s\n", kDemoFramePath);
        }
        if (demo_mode && !demo_scored_frame_saved &&
            game.score().hits >= 3u && run_state == RUN_PLAY) {
            demo_scored_frame_saved = screen.savePpm(kDemoScoredFramePath);
            if (demo_scored_frame_saved)
                printf("Scored demo frame saved: %s\n", kDemoScoredFramePath);
        }
        screen.present();
        sysUtilCheckCallback();

        ++diagnostic_frame;
        const char* diagnostic_phase = run_state == RUN_AUDIO_ERROR ? "audio_error" :
            tracking_paused
                ? (tracking_recalibrating ? "tracking_recalibration" : "tracking_pause") :
            demo_mode ? "demo_play" :
            paused ? "paused" :
            (calibrating ? "calibrating" :
             (run_state == RUN_PLAY ? "play" :
              (run_state == RUN_RESULTS ? "results" : "select")));
        if (diagnostic_frame % 120u == 0u ||
            strcmp(diagnostic_phase, previous_diagnostic_phase) != 0) {
            diagnostic.state = "running";
            diagnostic.phase = diagnostic_phase;
            diagnostic.frame = diagnostic_frame;
            diagnostic.uptime_ms = static_cast<unsigned int>(
                (nowMicroseconds() - process_start_us) / 1000ull);
            const movebeat::ps3::FrameTimingSample work = frame_work_timing.take();
            const movebeat::ps3::FrameTimingSample wait = frame_wait_timing.take();
            const movebeat::ps3::FrameTimingSample active = frame_active_timing.take();
            const movebeat::ps3::FrameTimingSample render = frame_render_timing.take();
            const movebeat::ps3::FrameTimingSample clear = frame_clear_timing.take();
            const movebeat::ps3::FrameTimingSample scene = frame_scene_timing.take();
            const movebeat::ps3::FrameTimingSample gpu = frame_gpu_notes_timing.take();
            diagnostic.frame_work_samples = work.count;
            diagnostic.frame_work_avg_us = work.average_us;
            diagnostic.frame_work_max_us = work.maximum_us;
            diagnostic.frame_work_over_60hz = work.over_60hz_budget;
            diagnostic.frame_wait_avg_us = wait.average_us;
            diagnostic.frame_active_avg_us = active.average_us;
            diagnostic.frame_active_max_us = active.maximum_us;
            diagnostic.frame_active_over_60hz = active.over_60hz_budget;
            diagnostic.frame_render_avg_us = render.average_us;
            diagnostic.frame_render_max_us = render.maximum_us;
            diagnostic.frame_clear_avg_us = clear.average_us;
            diagnostic.frame_scene_avg_us = scene.average_us;
            diagnostic.frame_gpu_notes_avg_us = gpu.average_us;
            ++diagnostic.heartbeat;
            diagnostic.level_count = static_cast<int>(level_choices.size());
            diagnostic.imported_level = imported_level ? 1 : 0;
            diagnostic.level_id = imported_level
                ? selected_level.source_key : "builtin_tutorial";
            diagnostic.chart_difficulty = imported_level &&
                selected_chart_index < selected_level.charts.size()
                ? selected_level.charts[selected_chart_index].difficulty : "Easy";
            diagnostic.left_connected = moves.left_connected ? 1 : 0;
            diagnostic.right_connected = moves.right_connected ? 1 : 0;
            diagnostic.left_tracked = moves.left.tracked ? 1 : 0;
            diagnostic.right_tracked = moves.right.tracked ? 1 : 0;
            diagnostic.hand_slots_swapped = hand_slots_swapped ? 1 : 0;
            diagnostic.hand_assignment_established =
                hand_assignment_established ? 1 : 0;
            diagnostic.invert_hands_setting = settings.invert_hands ? 1 : 0;
            diagnostic.hue_values_known = gem.hueValuesKnown() ? 1 : 0;
            diagnostic.hand_hues_applied =
                gem.handHueAssignmentApplied() ? 1 : 0;
            diagnostic.hue_assignment_pending =
                gem.hueAssignmentPending() ? 1 : 0;
            diagnostic.gem_slot0_hue = gem.hueForPhysicalSlot(0);
            diagnostic.gem_slot1_hue = gem.hueForPhysicalSlot(1);
            diagnostic.hue_error_code =
                static_cast<unsigned int>(gem.lastHueError());
            diagnostic.tracking_paused = tracking_paused ? 1 : 0;
            diagnostic.tracking_recalibrating = tracking_recalibrating ? 1 : 0;
            diagnostic.gem_update_ok = gem_update_ok ? 1 : 0;
            diagnostic.gem_update_stage =
                static_cast<unsigned int>(gem.lastUpdateStage());
            diagnostic.gem_error_code =
                static_cast<unsigned int>(gem.lastError());
            diagnostic.camera_read_count = gem.lastCameraReadCount();
            diagnostic.gem_connected_count = gem.lastGemConnectedCount();
            diagnostic.inertial_ready_mask = gem.lastInertialReadyMask();
            diagnostic.button_ready_mask = gem.lastButtonReadyMask();
            diagnostic.left_buttons = moves.left_buttons;
            diagnostic.right_buttons = moves.right_buttons;
            diagnostic.audio_active = audio_clock ? 1 : 0;
            diagnostic.audio_time_ms = audio_clock ? audio.timeMs() : 0u;
            diagnostic.capture_active = gem.captureActive() ? 1 : 0;
            diagnostic.score = game.score().score;
            diagnostic.hits = game.score().hits;
            diagnostic.misses = game.score().misses;
            diagnostic.demo_cycles = demo_cycles;
            movebeat::ps3::writeRunStatus(kRunStatusPath, diagnostic);
            previous_diagnostic_phase = diagnostic_phase;
        }
    }

    diagnostic.state = "clean_exit";
    diagnostic.phase = "shutdown";
    diagnostic.frame = diagnostic_frame;
    diagnostic.uptime_ms = static_cast<unsigned int>(
        (nowMicroseconds() - process_start_us) / 1000ull);
    const movebeat::ps3::FrameTimingSample work = frame_work_timing.take();
    const movebeat::ps3::FrameTimingSample wait = frame_wait_timing.take();
    const movebeat::ps3::FrameTimingSample active = frame_active_timing.take();
    const movebeat::ps3::FrameTimingSample render = frame_render_timing.take();
    const movebeat::ps3::FrameTimingSample clear = frame_clear_timing.take();
    const movebeat::ps3::FrameTimingSample scene = frame_scene_timing.take();
    const movebeat::ps3::FrameTimingSample gpu = frame_gpu_notes_timing.take();
    if (work.count) {
        diagnostic.frame_work_samples = work.count;
        diagnostic.frame_work_avg_us = work.average_us;
        diagnostic.frame_work_max_us = work.maximum_us;
        diagnostic.frame_work_over_60hz = work.over_60hz_budget;
        diagnostic.frame_wait_avg_us = wait.average_us;
        diagnostic.frame_active_avg_us = active.average_us;
        diagnostic.frame_active_max_us = active.maximum_us;
        diagnostic.frame_active_over_60hz = active.over_60hz_budget;
        diagnostic.frame_render_avg_us = render.average_us;
        diagnostic.frame_render_max_us = render.maximum_us;
        diagnostic.frame_clear_avg_us = clear.average_us;
        diagnostic.frame_scene_avg_us = scene.average_us;
        diagnostic.frame_gpu_notes_avg_us = gpu.average_us;
    }
    ++diagnostic.heartbeat;
    movebeat::ps3::writeRunStatus(kRunStatusPath, diagnostic);
    audio.shutdown();
    gem.shutdown();
    screen.shutdown();
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    return 0;
}
