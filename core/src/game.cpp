#include "movebeat/game.hpp"

#include <algorithm>
#include <cmath>

namespace movebeat {
namespace {

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float rowTravelPitch(float camera_pitch) {
    // Keep the complete note grid on parallel sightline-aligned paths. If the
    // upper row stays horizontal in world space while the other rows follow
    // the camera pitch, it can read as falling toward the player instead of
    // passing overhead. The shared slope preserves each row's spacing while
    // perspective fans the upper and lower rows away from the middle row.
    return camera_pitch;
}

float pointSegmentDistanceSquared(const Vec3& point, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const float denominator = lengthSquared(ab);
    if (denominator < 0.000001f) {
        return lengthSquared(point - a);
    }
    const float t = clamp01(dot(point - a, ab) / denominator);
    const Vec3 nearest = a + ab * t;
    return lengthSquared(point - nearest);
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

float pointTriangleDistanceSquared(const Vec3& point, const Vec3& a,
                                   const Vec3& b, const Vec3& c) {
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 normal = cross(ab, ac);
    const float normal_squared = lengthSquared(normal);
    if (normal_squared < 0.000001f) {
        return std::min(pointSegmentDistanceSquared(point, a, b),
               std::min(pointSegmentDistanceSquared(point, b, c),
                        pointSegmentDistanceSquared(point, c, a)));
    }

    const float signed_distance_numerator = dot(point - a, normal);
    const Vec3 projected = point - normal *
        (signed_distance_numerator / normal_squared);
    const Vec3 ap = projected - a;
    const float d00 = dot(ab, ab);
    const float d01 = dot(ab, ac);
    const float d11 = dot(ac, ac);
    const float d20 = dot(ap, ab);
    const float d21 = dot(ap, ac);
    const float denominator = d00 * d11 - d01 * d01;
    if (denominator > 0.000001f) {
        const float u = (d11 * d20 - d01 * d21) / denominator;
        const float v = (d00 * d21 - d01 * d20) / denominator;
        if (u >= 0.0f && v >= 0.0f && u + v <= 1.0f) {
            return signed_distance_numerator * signed_distance_numerator /
                   normal_squared;
        }
    }

    return std::min(pointSegmentDistanceSquared(point, a, b),
           std::min(pointSegmentDistanceSquared(point, b, c),
                    pointSegmentDistanceSquared(point, c, a)));
}

float pointSweptBladeDistanceSquared(const Vec3& point,
                                     const SaberPose& previous,
                                     const SaberPose& current) {
    // The blade sweeps a quad between its previous and current poses. Splitting
    // it into triangles lets the whole virtual blade, not just its tracked tip,
    // cut targets. Each endpoint moves relative to the passing note center.
    const float first_half = pointTriangleDistanceSquared(
        point, previous.handle, previous.tip, current.tip);
    const float second_half = pointTriangleDistanceSquared(
        point, previous.handle, current.tip, current.handle);
    return std::min(first_half, second_half);
}

Vec3 expectedDirection(CutDirection direction) {
    switch (direction) {
        case CUT_UP: return Vec3(0.0f, 1.0f, 0.0f);
        case CUT_DOWN: return Vec3(0.0f, -1.0f, 0.0f);
        case CUT_LEFT: return Vec3(-1.0f, 0.0f, 0.0f);
        case CUT_RIGHT: return Vec3(1.0f, 0.0f, 0.0f);
        case CUT_UP_LEFT: return normalized(Vec3(-1.0f, 1.0f, 0.0f));
        case CUT_UP_RIGHT: return normalized(Vec3(1.0f, 1.0f, 0.0f));
        case CUT_DOWN_LEFT: return normalized(Vec3(-1.0f, -1.0f, 0.0f));
        case CUT_DOWN_RIGHT: return normalized(Vec3(1.0f, -1.0f, 0.0f));
        case CUT_ANY: break;
    }
    return Vec3();
}

Vec3 rotatedDirection(CutDirection direction, float angle_offset_degrees) {
    const Vec3 base = expectedDirection(direction);
    if (length(base) == 0.0f || angle_offset_degrees == 0.0f) return base;
    const float angle = std::fmod(angle_offset_degrees, 360.0f) *
                        0.01745329251994329577f;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return Vec3(base.x * cosine - base.y * sine,
                base.x * sine + base.y * cosine, 0.0f);
}

unsigned int multiplierForCombo(unsigned int combo) {
    if (combo >= 16) return 8;
    if (combo >= 8) return 4;
    if (combo >= 4) return 2;
    return 1;
}

} // namespace

GameConfig::GameConfig()
    : lane_spacing(0.55f),
      row_spacing(0.55f),
      note_travel_speed(4.0f),
      note_spawn_depth(5.0f),
      row_pitch_per_depth(0.0f),
      note_radius(0.28f),
      arc_follow_radius(0.36f),
      minimum_swing_speed(1.5f),
      minimum_direction_dot(0.55f),
      maximum_sample_gap_ms(100),
      early_window_ms(150),
      late_window_ms(180),
      swing_angle_window_ms(200),
      pre_swing_target_degrees(100.0f),
      post_swing_target_degrees(60.0f),
      energy_failure(true) {}

bool configureNoteJump(GameConfig& config, float bpm, float note_jump_speed,
                       float start_beat_offset) {
    if (!std::isfinite(bpm) || bpm <= 0.0f ||
        !std::isfinite(note_jump_speed) || note_jump_speed < 0.1f ||
        note_jump_speed > 40.0f || !std::isfinite(start_beat_offset) ||
        start_beat_offset < -16.0f || start_beat_offset > 16.0f) return false;

    const float seconds_per_beat = 60.0f / bpm;
    float half_jump_beats = 4.0f;
    for (unsigned int iteration = 0; iteration < 16 &&
         note_jump_speed * half_jump_beats * seconds_per_beat > 18.0f;
         ++iteration) {
        half_jump_beats *= 0.5f;
    }
    half_jump_beats += start_beat_offset;
    if (half_jump_beats < 0.25f) half_jump_beats = 0.25f;

    // Game-space is intentionally smaller than real-world meters to keep the
    // legacy camera and note grid legible on a 720p television.
    const float world_units_per_meter = 0.4f;
    const float speed = note_jump_speed * world_units_per_meter;
    const float depth = speed * half_jump_beats * seconds_per_beat;
    if (!std::isfinite(depth) || depth < 0.25f || depth > 24.0f) return false;

    config.note_travel_speed = speed;
    config.note_spawn_depth = depth;
    return true;
}

ScoreState::ScoreState()
    : score(0), combo(0), max_combo(0), hits(0), misses(0), bombs_hit(0),
      bombs_avoided(0), obstacles_hit(0), obstacles_avoided(0),
      chain_links_hit(0), chain_links_missed(0), arcs_hit(0), arcs_missed(0),
      energy(50), failed(false) {}

Game::Game(const GameConfig& config)
    : config_(config), have_previous_pose_(false), previous_time_ms_(0) {}

void Game::setConfig(const GameConfig& config) {
    config_ = config;
    resetInputContinuity();
}

float Game::noteSpawnDepth() const {
    return config_.note_spawn_depth;
}

void Game::load(const Chart& chart) {
    chart_ = chart;
    reset();
}

void Game::reset() {
    states_.assign(chart_.notes.size(), NOTE_PENDING);
    obstacle_states_.assign(chart_.obstacles.size(), NOTE_PENDING);
    arc_followed_ms_.assign(chart_.arcs.size(), 0u);
    arc_states_.assign(chart_.arcs.size(), NOTE_PENDING);
    invalidateInput();
    score_ = ScoreState();
}

void Game::resetInputContinuity() {
    previous_left_ = SaberPose();
    previous_right_ = SaberPose();
    left_swing_ = SwingTracker();
    right_swing_ = SwingTracker();
    have_previous_pose_ = false;
    previous_time_ms_ = 0;
}

void Game::invalidateInput() {
    resetInputContinuity();
    pending_cuts_.clear();
    events_ = FrameEvents();
}

Vec3 Game::notePosition(const Note& note, unsigned int now_ms) const {
    const float x = (static_cast<float>(note.lane) - 1.5f) * config_.lane_spacing;
    const int delta_ms = static_cast<int>(note.time_ms) - static_cast<int>(now_ms);
    const float z = static_cast<float>(delta_ms) *
                    (config_.note_travel_speed * 0.001f);
    // Beat Saber maps use three vertical rows (0..2), centered on row 1.
    // All rows follow the pitched camera's sightline, retaining their authored
    // vertical separation while perspective fans them apart toward the player.
    const float row_offset = static_cast<float>(note.row) - 1.0f;
    const float row_pitch = rowTravelPitch(config_.row_pitch_per_depth);
    const float y = row_offset * config_.row_spacing + z * row_pitch;
    return Vec3(x, y, z);
}

Vec3 Game::obstacleMinimum(const Obstacle& obstacle, unsigned int now_ms) const {
    const float x = (static_cast<float>(obstacle.lane) - 2.0f) *
        config_.lane_spacing;
    const float y = (static_cast<float>(obstacle.row) - 1.5f) *
        config_.row_spacing;
    const int delta_ms = static_cast<int>(obstacle.time_ms) -
        static_cast<int>(now_ms);
    return Vec3(x, y, static_cast<float>(delta_ms) *
        (config_.note_travel_speed * 0.001f));
}

Vec3 Game::obstacleMaximum(const Obstacle& obstacle, unsigned int now_ms) const {
    const Vec3 minimum = obstacleMinimum(obstacle, now_ms);
    return Vec3(minimum.x + static_cast<float>(obstacle.width) * config_.lane_spacing,
                minimum.y + static_cast<float>(obstacle.height) * config_.row_spacing,
                minimum.z + static_cast<float>(obstacle.duration_ms) *
                    (config_.note_travel_speed * 0.001f));
}

Vec3 Game::arcPoint(const Arc& arc, unsigned int now_ms, float progress) const {
    progress = clamp01(progress);
    const float head_x = (static_cast<float>(arc.head_lane) - 1.5f) *
        config_.lane_spacing;
    const float head_y = (static_cast<float>(arc.head_row) - 1.0f) *
        config_.row_spacing;
    const float tail_x = (static_cast<float>(arc.tail_lane) - 1.5f) *
        config_.lane_spacing;
    const float tail_y = (static_cast<float>(arc.tail_row) - 1.0f) *
        config_.row_spacing;
    const float time_ms = static_cast<float>(arc.head_time_ms) +
        (static_cast<float>(arc.tail_time_ms) -
         static_cast<float>(arc.head_time_ms)) * progress;
    const float z = (time_ms - static_cast<float>(now_ms)) *
                    (config_.note_travel_speed * 0.001f);
    const Vec3 head(head_x, head_y, 0.0f);
    const Vec3 tail(tail_x, tail_y, 0.0f);
    const Vec3 head_tangent = expectedDirection(arc.head_direction);
    const Vec3 tail_tangent = expectedDirection(arc.tail_direction);
    const float span = std::max(config_.lane_spacing,
                                length(tail - head));
    const Vec3 chord = tail - head;
    const Vec3 control1 = arc.head_direction == CUT_ANY
        ? head + chord * (1.0f / 3.0f)
        : head + head_tangent *
            (span * arc.head_control_point_multiplier / 3.0f);
    const Vec3 control2 = arc.tail_direction == CUT_ANY
        ? head + chord * (2.0f / 3.0f)
        : tail - tail_tangent *
            (span * arc.tail_control_point_multiplier / 3.0f);
    const auto cubic = [](const Vec3& a, const Vec3& b, const Vec3& c,
                          const Vec3& d, float t) {
        const float inverse_t = 1.0f - t;
        const float a_weight = inverse_t * inverse_t * inverse_t;
        const float b_weight = 3.0f * inverse_t * inverse_t * t;
        const float c_weight = 3.0f * inverse_t * t * t;
        const float d_weight = t * t * t;
        return a * a_weight + b * b_weight + c * c_weight + d * d_weight;
    };
    const Vec3 base_point = cubic(head, control1, control2, tail, progress);
    Vec3 path_point = base_point;

    // Beat Saber mid-anchor rotation is defined only for arcs in the same
    // column whose cut directions are parallel or antiparallel. Split the
    // ordinary cubic at t=0.5 (De Casteljau), then move the shared midpoint
    // sideways. Moving the adjacent split controls by the same offset keeps
    // the two halves position- and tangent-continuous and preserves both ends.
    if (arc.mid_anchor_mode != 0 && arc.head_lane == arc.tail_lane &&
        arc.head_direction != CUT_ANY && arc.tail_direction != CUT_ANY) {
        const Vec3 head_direction = normalized(head_tangent);
        const Vec3 tail_direction = normalized(tail_tangent);
        const float direction_alignment = dot(head_direction, tail_direction);
        if (std::fabs(std::fabs(direction_alignment) - 1.0f) < 0.001f) {
            const Vec3 left_control = (head + control1) * 0.5f;
            const Vec3 middle_control = (control1 + control2) * 0.5f;
            const Vec3 right_control = (control2 + tail) * 0.5f;
            const Vec3 left_middle_control =
                (left_control + middle_control) * 0.5f;
            const Vec3 right_middle_control =
                (middle_control + right_control) * 0.5f;
            const Vec3 base_midpoint =
                (left_middle_control + right_middle_control) * 0.5f;

            // Grid coordinates use a positive-up Y axis. Rotating the head
            // direction by -90 degrees gives clockwise (mode 1); +90 gives
            // counter-clockwise (mode 2). 2.5 cells is the map-format anchor
            // reach, scaled to this game's world-space lane spacing.
            const float rotation_sign = arc.mid_anchor_mode == 1 ? -1.0f : 1.0f;
            const Vec3 anchor_direction(
                -rotation_sign * head_direction.y,
                rotation_sign * head_direction.x, 0.0f);
            const Vec3 offset = anchor_direction *
                (config_.lane_spacing * 2.5f);
            const Vec3 adjusted_midpoint = base_midpoint + offset;
            const Vec3 adjusted_left_control = left_middle_control + offset;
            const Vec3 adjusted_right_control = right_middle_control + offset;

            if (progress < 0.5f) {
                path_point = cubic(head, left_control, adjusted_left_control,
                                   adjusted_midpoint, progress * 2.0f);
            } else {
                path_point = cubic(adjusted_midpoint, adjusted_right_control,
                                   right_control, tail,
                                   (progress - 0.5f) * 2.0f);
            }
        }
    }
    const float row_pitch = rowTravelPitch(config_.row_pitch_per_depth);
    return Vec3(path_point.x, path_point.y + z * row_pitch, z);
}

bool Game::tryCut(const Note& note, unsigned int now_ms,
                  const SaberPose& previous, const SaberPose& current,
                  unsigned int frame_delta_ms, unsigned int& quality,
                  bool require_swing, float pre_swing_degrees) const {
    if (!previous.tracked || !current.tracked) return false;

    const int timing_error = std::abs(static_cast<int>(now_ms) - static_cast<int>(note.time_ms));
    if (now_ms < note.time_ms && static_cast<unsigned int>(timing_error) > config_.early_window_ms) return false;
    if (now_ms >= note.time_ms && static_cast<unsigned int>(timing_error) > config_.late_window_ms) return false;

    const unsigned int previous_time_ms = now_ms >= frame_delta_ms
        ? now_ms - frame_delta_ms : now_ms;
    const Vec3 previous_note_position = notePosition(note, previous_time_ms);
    const Vec3 current_note_position = notePosition(note, now_ms);
    const SaberPose relative_previous(
        previous.handle - previous_note_position,
        previous.tip - previous_note_position, true);
    const SaberPose relative_current(
        current.handle - current_note_position,
        current.tip - current_note_position, true);
    const float center_distance_squared = pointSweptBladeDistanceSquared(
        Vec3(), relative_previous, relative_current);
    if (center_distance_squared > config_.note_radius * config_.note_radius) {
        return false;
    }

    const Vec3 swing = current.tip - previous.tip;
    const float swing_speed = frame_delta_ms > 0
        ? length(swing) * 1000.0f / static_cast<float>(frame_delta_ms)
        : 0.0f;
    if (require_swing && swing_speed < config_.minimum_swing_speed) return false;

    if (note.direction != CUT_ANY) {
        const float direction_score = dot(normalized(swing),
            rotatedDirection(note.direction, note.angle_offset_degrees));
        if (direction_score < config_.minimum_direction_dot) return false;
    }

    const float center_ratio = config_.note_radius > 0.0f
        ? clamp01(1.0f - std::sqrt(center_distance_squared) / config_.note_radius)
        : 0.0f;
    quality = static_cast<unsigned int>(center_ratio * 15.0f + 0.5f);
    if (require_swing && config_.pre_swing_target_degrees > 0.0f) {
        const float pre_ratio = clamp01(
            pre_swing_degrees / config_.pre_swing_target_degrees);
        quality += static_cast<unsigned int>(pre_ratio * 70.0f + 0.5f);
    }
    return true;
}

unsigned int Game::registerHit(unsigned int quality) {
    const unsigned int multiplier = multiplierForCombo(score_.combo);
    score_.score += quality * multiplier;
    ++score_.hits;
    ++score_.combo;
    score_.max_combo = std::max(score_.max_combo, score_.combo);
    score_.energy = std::min(100u, score_.energy + 1u);
    return multiplier;
}

void Game::registerMiss(unsigned int penalty) {
    ++score_.misses;
    score_.combo = 0;
    score_.energy = penalty >= score_.energy ? 0u : score_.energy - penalty;
    score_.failed = config_.energy_failure && score_.energy == 0;
}

float Game::updateSwingTracker(SwingTracker& tracker, unsigned int now_ms,
                               const SaberPose& pose) {
    const Vec3 blade = pose.tip - pose.handle;
    if (!pose.tracked || lengthSquared(blade) < 0.000001f) {
        tracker = SwingTracker();
        return 0.0f;
    }

    // Repeated timestamps carry no elapsed motion. Keep the last timed pose.
    if (tracker.have_direction && now_ms == tracker.time_ms) return 0.0f;
    const Vec3 direction = normalized(blade);
    float delta_degrees = 0.0f;
    if (tracker.have_direction && now_ms >= tracker.time_ms &&
        now_ms - tracker.time_ms <= config_.swing_angle_window_ms) {
        const float cosine = std::max(-1.0f, std::min(1.0f,
            dot(tracker.blade_direction, direction)));
        delta_degrees = std::acos(cosine) * 57.2957795f;
        if (delta_degrees > 0.01f)
            tracker.samples.push_back(AngularSample(now_ms, delta_degrees));
    } else {
        tracker.samples.clear();
    }
    tracker.blade_direction = direction;
    tracker.time_ms = now_ms;
    tracker.have_direction = true;

    while (!tracker.samples.empty() && now_ms >= tracker.samples.front().time_ms &&
           now_ms - tracker.samples.front().time_ms > config_.swing_angle_window_ms) {
        tracker.samples.pop_front();
    }
    return delta_degrees;
}

float Game::trackedSwingDegrees(const SwingTracker& tracker) const {
    float degrees = 0.0f;
    for (std::deque<AngularSample>::const_iterator sample = tracker.samples.begin();
         sample != tracker.samples.end(); ++sample) {
        degrees += sample->degrees;
    }
    return degrees;
}

void Game::updatePendingCuts(unsigned int now_ms, float left_delta_degrees,
                             float right_delta_degrees) {
    for (std::size_t index = 0; index < pending_cuts_.size();) {
        PendingCut& cut = pending_cuts_[index];
        // Settle before accumulating: the current frame may already be outside
        // the follow-through window even when its inter-frame gap is small.
        if (now_ms < cut.cut_time_ms ||
            now_ms - cut.cut_time_ms > config_.swing_angle_window_ms) {
            pending_cuts_.erase(pending_cuts_.begin() + index);
            continue;
        }
        cut.post_degrees += cut.hand == HAND_LEFT
            ? left_delta_degrees : right_delta_degrees;
        const float ratio = config_.post_swing_target_degrees > 0.0f
            ? clamp01(cut.post_degrees / config_.post_swing_target_degrees)
            : 1.0f;
        const unsigned int earned = static_cast<unsigned int>(ratio * 30.0f + 0.5f);
        if (earned > cut.awarded_bonus) {
            score_.score += (earned - cut.awarded_bonus) * cut.multiplier;
            cut.awarded_bonus = earned;
        }
        const bool expired = now_ms >= cut.cut_time_ms &&
            now_ms - cut.cut_time_ms >= config_.swing_angle_window_ms;
        if (cut.awarded_bonus >= 30u || expired) {
            pending_cuts_.erase(pending_cuts_.begin() + index);
        } else {
            ++index;
        }
    }
}

void Game::update(unsigned int now_ms, const SaberPose& left, const SaberPose& right) {
    events_ = FrameEvents();
    if (score_.failed) return;
    // A long input gap does not tell us the path of the blade between samples.
    // Never interpret the resulting teleport as a cut through every note.
    if (have_previous_pose_ &&
        (now_ms < previous_time_ms_ ||
         now_ms - previous_time_ms_ > config_.maximum_sample_gap_ms))
        resetInputContinuity();
    unsigned int frame_delta_ms = 0;
    if (!have_previous_pose_) {
        previous_left_ = left;
        previous_right_ = right;
        have_previous_pose_ = true;
    } else if (now_ms >= previous_time_ms_) {
        frame_delta_ms = now_ms - previous_time_ms_;
    }
    const float left_angle_delta = updateSwingTracker(left_swing_, now_ms, left);
    const float right_angle_delta = updateSwingTracker(right_swing_, now_ms, right);
    updatePendingCuts(now_ms, left_angle_delta, right_angle_delta);

    for (std::size_t i = 0; i < chart_.obstacles.size(); ++i) {
        if (obstacle_states_[i] != NOTE_PENDING) continue;
        const Obstacle& obstacle = chart_.obstacles[i];
        const unsigned long long end_ms = static_cast<unsigned long long>(
            obstacle.time_ms) + obstacle.duration_ms;
        if (static_cast<unsigned long long>(now_ms) > end_ms) {
            obstacle_states_[i] = NOTE_MISSED;
            ++score_.obstacles_avoided;
            continue;
        }
        if (now_ms < obstacle.time_ms) continue;
        const Vec3 minimum = obstacleMinimum(obstacle, now_ms);
        const Vec3 maximum = obstacleMaximum(obstacle, now_ms);
        const bool left_inside = left.tracked &&
            left.tip.x >= minimum.x && left.tip.x <= maximum.x &&
            left.tip.y >= minimum.y && left.tip.y <= maximum.y;
        const bool right_inside = right.tracked &&
            right.tip.x >= minimum.x && right.tip.x <= maximum.x &&
            right.tip.y >= minimum.y && right.tip.y <= maximum.y;
        if (left_inside || right_inside) {
            obstacle_states_[i] = NOTE_HIT;
            ++score_.obstacles_hit;
            registerMiss(12);
            events_.miss = true;
            events_.obstacle_hit = true;
        }
    }

    for (std::size_t i = 0; i < chart_.notes.size(); ++i) {
        if (states_[i] != NOTE_PENDING) continue;
        const Note& note = chart_.notes[i];

        if (now_ms > note.time_ms + config_.late_window_ms) {
            states_[i] = NOTE_MISSED;
            if (note.kind == NOTE_BOMB) ++score_.bombs_avoided;
            else {
                if (note.kind == NOTE_CHAIN) {
                    ++score_.chain_links_missed;
                    registerMiss(5);
                } else {
                    registerMiss();
                }
                events_.miss = true;
            }
            continue;
        }

        if (note.kind == NOTE_BOMB) {
            unsigned int ignored_quality = 0;
            bool touched = false;
            if (tryCut(note, now_ms, previous_left_, left,
                       frame_delta_ms, ignored_quality, false)) {
                touched = true;
            }
            if (tryCut(note, now_ms, previous_right_, right,
                       frame_delta_ms, ignored_quality, false)) {
                touched = true;
            }
            if (touched) {
                states_[i] = NOTE_HIT;
                ++score_.bombs_hit;
                registerMiss(15);
                events_.miss = true;
                events_.bomb_hit = true;
            }
            continue;
        }

        const SaberPose& previous = note.hand == HAND_LEFT ? previous_left_ : previous_right_;
        const SaberPose& current = note.hand == HAND_LEFT ? left : right;
        unsigned int quality = 0;
        const float pre_swing_degrees = note.hand == HAND_LEFT
            ? trackedSwingDegrees(left_swing_) : trackedSwingDegrees(right_swing_);
        if (tryCut(note, now_ms, previous, current, frame_delta_ms, quality,
                   true, pre_swing_degrees)) {
            states_[i] = NOTE_HIT;
            if (note.kind == NOTE_CHAIN) {
                ++score_.chain_links_hit;
                registerHit(20);
            } else {
                const unsigned int multiplier = registerHit(quality);
                pending_cuts_.push_back(PendingCut(note.hand, now_ms, multiplier));
            }
            if (note.hand == HAND_LEFT) events_.left_hit = true;
            else events_.right_hit = true;
            if (events_.cut_visual_count < FrameEvents::MAX_CUT_VISUALS) {
                FrameEvents::CutVisual& visual =
                    events_.cut_visuals[events_.cut_visual_count++];
                visual.position = notePosition(note, now_ms);
                visual.hand = note.hand;
                visual.kind = note.kind;
            }
        }
    }

    // Count covered time, not per-frame points, so arc score is independent
    // of frame rate. The assigned saber must follow the moving cubic target.
    for (std::size_t i = 0; i < chart_.arcs.size(); ++i) {
        if (arc_states_[i] != NOTE_PENDING) continue;
        const Arc& arc = chart_.arcs[i];
        const unsigned int duration = arc.tail_time_ms - arc.head_time_ms;
        if (now_ms >= arc.head_time_ms && now_ms <= arc.tail_time_ms &&
            frame_delta_ms > 0 && previous_time_ms_ < now_ms) {
            const unsigned int segment_start = std::max(previous_time_ms_, arc.head_time_ms);
            const unsigned int segment_end = std::min(now_ms, arc.tail_time_ms);
            const unsigned int covered_interval = segment_end > segment_start
                ? segment_end - segment_start : 0u;
            const SaberPose& previous = arc.hand == HAND_LEFT
                ? previous_left_ : previous_right_;
            const SaberPose& current = arc.hand == HAND_LEFT ? left : right;
            if (covered_interval > 0 && previous.tracked && current.tracked) {
                const float previous_progress = duration > 0
                    ? static_cast<float>(segment_start - arc.head_time_ms) / duration : 0.0f;
                const float current_progress = duration > 0
                    ? static_cast<float>(segment_end - arc.head_time_ms) / duration : 1.0f;
                const Vec3 previous_target = arcPoint(arc, segment_start, previous_progress);
                const Vec3 current_target = arcPoint(arc, segment_end, current_progress);
                const SaberPose relative_previous(
                    previous.handle - previous_target, previous.tip - previous_target, true);
                const SaberPose relative_current(
                    current.handle - current_target, current.tip - current_target, true);
                if (pointSweptBladeDistanceSquared(Vec3(), relative_previous,
                        relative_current) <= config_.arc_follow_radius *
                        config_.arc_follow_radius) {
                    arc_followed_ms_[i] = std::min(duration,
                        arc_followed_ms_[i] + covered_interval);
                }
            }
        }
        if (now_ms > arc.tail_time_ms) {
            arc_states_[i] = NOTE_MISSED;
            const unsigned int required_ms = (duration / 100u) * 60u +
                ((duration % 100u) * 60u + 99u) / 100u;
            if (duration > 0 && arc_followed_ms_[i] >= required_ms) {
                ++score_.arcs_hit;
                const unsigned int quality = static_cast<unsigned int>(
                    static_cast<unsigned long long>(arc_followed_ms_[i]) * 50u /
                    duration);
                registerHit(quality);
            } else {
                ++score_.arcs_missed;
                registerMiss(8);
                events_.miss = true;
            }
        }
    }

    previous_left_ = left;
    previous_right_ = right;
    previous_time_ms_ = now_ms;
}

const ScoreState& Game::score() const {
    return score_;
}

const FrameEvents& Game::events() const {
    return events_;
}

std::size_t Game::remainingNotes() const {
    std::size_t count = 0;
    for (std::size_t i = 0; i < states_.size(); ++i) {
        if (states_[i] == NOTE_PENDING) ++count;
    }
    return count;
}

std::size_t Game::noteCount() const {
    return chart_.notes.size();
}

const Note& Game::note(std::size_t index) const {
    return chart_.notes[index];
}

bool Game::notePending(std::size_t index) const {
    return index < states_.size() && states_[index] == NOTE_PENDING;
}

std::size_t Game::obstacleCount() const {
    return chart_.obstacles.size();
}

const Obstacle& Game::obstacle(std::size_t index) const {
    return chart_.obstacles[index];
}

bool Game::obstaclePending(std::size_t index) const {
    return index < obstacle_states_.size() && obstacle_states_[index] == NOTE_PENDING;
}

std::size_t Game::arcCount() const {
    return chart_.arcs.size();
}

const Arc& Game::arc(std::size_t index) const {
    return chart_.arcs[index];
}

bool Game::finished(unsigned int now_ms) const {
    if (chart_.notes.empty() && chart_.obstacles.empty() && chart_.arcs.empty())
        return true;
    if (score_.failed) return true;
    if (remainingNotes() != 0) return false;
    for (std::size_t i = 0; i < obstacle_states_.size(); ++i)
        if (obstacle_states_[i] == NOTE_PENDING) return false;
    for (std::size_t i = 0; i < arc_states_.size(); ++i)
        if (arc_states_[i] == NOTE_PENDING && now_ms <= chart_.arcs[i].tail_time_ms)
            return false;
    if (!pending_cuts_.empty()) return false;
    return true;
}

} // namespace movebeat
