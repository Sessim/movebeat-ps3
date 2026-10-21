#ifndef MOVEBEAT_GAME_HPP
#define MOVEBEAT_GAME_HPP

#include <cstddef>
#include <deque>
#include <vector>

#include "movebeat/chart.hpp"

namespace movebeat {

struct GameConfig {
    float lane_spacing;
    float row_spacing;
    float note_travel_speed;
    float note_spawn_depth;
    // World-space vertical correction per unit of travel depth for the middle
    // and lower note rows in a pitched spectator camera.
    float row_pitch_per_depth;
    float note_radius;
    float arc_follow_radius;
    float minimum_swing_speed;
    float minimum_direction_dot;
    unsigned int maximum_sample_gap_ms;
    unsigned int early_window_ms;
    unsigned int late_window_ms;
    unsigned int swing_angle_window_ms;
    float pre_swing_target_degrees;
    float post_swing_target_degrees;
    // Empty energy fails the level; off ("no fail"), energy still drains
    // and shows but the song plays to its end.
    bool energy_failure;

    GameConfig();
};

// Applies Beat Saber NJS and spawn-offset metadata using the standard half-
// jump-duration calculation. Returns false for values unsafe to render.
bool configureNoteJump(GameConfig& config, float bpm, float note_jump_speed,
                       float start_beat_offset);

struct ScoreState {
    unsigned int score;
    unsigned int combo;
    unsigned int max_combo;
    unsigned int hits;
    unsigned int misses;
    unsigned int bombs_hit;
    unsigned int bombs_avoided;
    unsigned int obstacles_hit;
    unsigned int obstacles_avoided;
    unsigned int chain_links_hit;
    unsigned int chain_links_missed;
    unsigned int arcs_hit;
    unsigned int arcs_missed;
    unsigned int energy;
    bool failed;

    ScoreState();
};

struct FrameEvents {
    struct CutVisual {
        Vec3 position;
        // Unit swing direction across the screen (z = 0): the cube is
        // split along it.
        Vec3 swing;
        Hand hand;
        NoteKind kind;

        CutVisual() : swing(1.0f, 0.0f, 0.0f), hand(HAND_LEFT), kind(NOTE_CUT) {}
    };
    enum { MAX_CUT_VISUALS = 16 };

    bool left_hit;
    bool right_hit;
    bool miss;
    bool bomb_hit;
    bool obstacle_hit;
    // A blade tip is inside a wall this frame (each wall scores once, but
    // touching it is felt for as long as it lasts).
    bool in_obstacle;
    // A running combo of at least one was lost this frame.
    bool combo_broken;
    unsigned int cut_visual_count;
    CutVisual cut_visuals[MAX_CUT_VISUALS];

    FrameEvents()
        : left_hit(false), right_hit(false), miss(false), bomb_hit(false),
          obstacle_hit(false), in_obstacle(false), combo_broken(false),
          cut_visual_count(0) {}
};

class Game {
public:
    explicit Game(const GameConfig& config = GameConfig());

    void setConfig(const GameConfig& config);
    const GameConfig& config() const { return config_; }
    float noteSpawnDepth() const;
    void load(const Chart& chart);
    void reset();
    void invalidateInput();
    void update(unsigned int now_ms, const SaberPose& left, const SaberPose& right);

    const ScoreState& score() const;
    const FrameEvents& events() const;
    bool finished(unsigned int now_ms) const;
    std::size_t remainingNotes() const;
    std::size_t noteCount() const;
    const Note& note(std::size_t index) const;
    bool notePending(std::size_t index) const;
    Vec3 notePosition(const Note& note, unsigned int now_ms) const;
    std::size_t obstacleCount() const;
    const Obstacle& obstacle(std::size_t index) const;
    bool obstaclePending(std::size_t index) const;
    Vec3 obstacleMinimum(const Obstacle& obstacle, unsigned int now_ms) const;
    Vec3 obstacleMaximum(const Obstacle& obstacle, unsigned int now_ms) const;
    std::size_t arcCount() const;
    const Arc& arc(std::size_t index) const;
    Vec3 arcPoint(const Arc& arc, unsigned int now_ms, float progress) const;
    // 1 while the matching blade follows an arc, fading to 0 within
    // kArcGlowMs after it leaves (the arc lights up as it is followed).
    float arcGlow(std::size_t index, unsigned int now_ms) const;
    static const unsigned int kArcGlowMs = 200u;

private:
    enum NoteState { NOTE_PENDING, NOTE_HIT, NOTE_MISSED };

    struct AngularSample {
        unsigned int time_ms;
        float degrees;

        AngularSample(unsigned int time, float angle)
            : time_ms(time), degrees(angle) {}
    };

    struct SwingTracker {
        Vec3 blade_direction;
        unsigned int time_ms;
        bool have_direction;
        std::deque<AngularSample> samples;

        SwingTracker() : time_ms(0), have_direction(false) {}
    };

    struct PendingCut {
        Hand hand;
        unsigned int cut_time_ms;
        float post_degrees;
        unsigned int awarded_bonus;
        unsigned int multiplier;

        PendingCut(Hand cut_hand, unsigned int time, unsigned int score_multiplier)
            : hand(cut_hand), cut_time_ms(time), post_degrees(0.0f),
              awarded_bonus(0), multiplier(score_multiplier) {}
    };

    bool tryCut(const Note& note, unsigned int now_ms,
                const SaberPose& previous, const SaberPose& current,
                unsigned int frame_delta_ms, unsigned int& quality,
                bool require_swing = true, float pre_swing_degrees = 0.0f) const;
    unsigned int registerHit(unsigned int quality);
    void registerMiss(unsigned int penalty = 10);
    float updateSwingTracker(SwingTracker& tracker, unsigned int now_ms,
                             const SaberPose& pose);
    float trackedSwingDegrees(const SwingTracker& tracker) const;
    void updatePendingCuts(unsigned int now_ms, float left_delta_degrees,
                           float right_delta_degrees);
    void resetInputContinuity();

    GameConfig config_;
    Chart chart_;
    std::vector<NoteState> states_;
    std::vector<NoteState> obstacle_states_;
    std::vector<unsigned int> arc_followed_ms_;
    // Song time the blade was last on each arc (0: never).
    std::vector<unsigned int> arc_on_at_ms_;
    std::vector<NoteState> arc_states_;
    SaberPose previous_left_;
    SaberPose previous_right_;
    SwingTracker left_swing_;
    SwingTracker right_swing_;
    std::vector<PendingCut> pending_cuts_;
    bool have_previous_pose_;
    unsigned int previous_time_ms_;
    ScoreState score_;
    FrameEvents events_;
};

} // namespace movebeat

#endif
