#ifndef MOVEBEAT_PS3_TRACKING_LOSS_LOG_HPP
#define MOVEBEAT_PS3_TRACKING_LOSS_LOG_HPP

#include <stdio.h>

#include <vector>

namespace movebeat {
namespace ps3 {

// One stretch of a song during which the camera did not see a Move's
// sphere: where the sphere was last seen (image coordinates 0..1, radius in
// pixels), and what libgem said when it went.
struct TrackingLoss {
    unsigned int slot;
    unsigned int song_ms;
    unsigned int duration_ms;
    float last_x;
    float last_y;
    float last_radius;
    bool last_known;
    bool near_edge;
    int result;
    unsigned int tracking_flags;
    unsigned long long status_flags;
};

// Collects the optical losses of both Moves during a song, so a session on
// the console tells whether spheres leave the frame, hide behind the body
// or vanish in the middle of the picture (lighting, colours, blur).
class TrackingLossLog {
public:
    enum { MAX_EVENTS = 256 };
    // A sphere last seen within this share of the frame's border probably
    // left the camera's view.
    static constexpr float kEdge = 0.08f;
    // Shorter losses are flicker; longer ones are felt in play.
    enum { SHORT_MS = 150 };

    TrackingLossLog() { reset(); }

    void reset() {
        events_.clear();
        for (int slot = 0; slot < 2; ++slot) {
            lost_[slot] = false;
            lost_since_ms_[slot] = 0;
            lost_song_ms_[slot] = 0;
            last_known_[slot] = false;
            count_[slot] = 0;
        }
        total_lost_ms_ = longest_ms_ = edge_count_ = short_count_ = 0;
    }

    // Once per frame of play for each physical slot. `image_valid` with
    // `x`, `y`, `radius` give the sphere in the camera image when seen;
    // `result`, `tracking_flags` and `status_flags` are libgem's view.
    void update(unsigned int slot, bool seen, unsigned int now_ms, unsigned int song_ms,
                bool image_valid, float x, float y, float radius, int result,
                unsigned int tracking_flags, unsigned long long status_flags) {
        if (slot > 1) return;
        if (seen) {
            if (lost_[slot]) finish(slot, now_ms);
            if (image_valid) {
                last_x_[slot] = x;
                last_y_[slot] = y;
                last_radius_[slot] = radius;
                last_known_[slot] = true;
            }
            return;
        }
        if (lost_[slot]) {
            status_[slot] |= status_flags;
            return;
        }
        lost_[slot] = true;
        lost_since_ms_[slot] = now_ms;
        lost_song_ms_[slot] = song_ms;
        result_[slot] = result;
        tracking_[slot] = tracking_flags;
        status_[slot] = status_flags;
    }

    // Closes the losses still open (end of the song).
    void close(unsigned int now_ms) {
        for (unsigned int slot = 0; slot < 2; ++slot)
            if (lost_[slot]) finish(slot, now_ms);
    }

    unsigned int count(unsigned int slot) const { return slot < 2 ? count_[slot] : 0; }
    unsigned int totalLostMs() const { return total_lost_ms_; }
    unsigned int longestMs() const { return longest_ms_; }
    unsigned int edgeCount() const { return edge_count_; }
    unsigned int shortCount() const { return short_count_; }
    const std::vector<TrackingLoss>& events() const { return events_; }

    bool writeCsv(const char* path) const {
        FILE* file = fopen(path, "w");
        if (!file) return false;
        fprintf(file, "slot,song_ms,duration_ms,last_x,last_y,last_radius_px,near_edge,"
                      "result,tracking_flags,status_flags\n");
        for (std::size_t index = 0; index < events_.size(); ++index) {
            const TrackingLoss& loss = events_[index];
            if (loss.last_known)
                fprintf(file, "%u,%u,%u,%.3f,%.3f,%.1f,%d,%d,%u,0x%llx\n", loss.slot,
                        loss.song_ms, loss.duration_ms, loss.last_x, loss.last_y,
                        loss.last_radius, loss.near_edge ? 1 : 0, loss.result,
                        loss.tracking_flags, loss.status_flags);
            else
                fprintf(file, "%u,%u,%u,,,,0,%d,%u,0x%llx\n", loss.slot, loss.song_ms,
                        loss.duration_ms, loss.result, loss.tracking_flags,
                        loss.status_flags);
        }
        return fclose(file) == 0;
    }

private:
    void finish(unsigned int slot, unsigned int now_ms) {
        lost_[slot] = false;
        const unsigned int duration = now_ms >= lost_since_ms_[slot]
            ? now_ms - lost_since_ms_[slot] : 0u;
        TrackingLoss loss;
        loss.slot = slot;
        loss.song_ms = lost_song_ms_[slot];
        loss.duration_ms = duration;
        loss.last_known = last_known_[slot];
        loss.last_x = last_known_[slot] ? last_x_[slot] : 0.0f;
        loss.last_y = last_known_[slot] ? last_y_[slot] : 0.0f;
        loss.last_radius = last_known_[slot] ? last_radius_[slot] : 0.0f;
        loss.near_edge = last_known_[slot] &&
            (loss.last_x < kEdge || loss.last_x > 1.0f - kEdge ||
             loss.last_y < kEdge || loss.last_y > 1.0f - kEdge);
        loss.result = result_[slot];
        loss.tracking_flags = tracking_[slot];
        loss.status_flags = status_[slot];
        ++count_[slot];
        total_lost_ms_ += duration;
        if (duration > longest_ms_) longest_ms_ = duration;
        if (loss.near_edge) ++edge_count_;
        if (duration < SHORT_MS) ++short_count_;
        if (events_.size() < MAX_EVENTS) events_.push_back(loss);
    }

    std::vector<TrackingLoss> events_;
    bool lost_[2];
    unsigned int lost_since_ms_[2];
    unsigned int lost_song_ms_[2];
    bool last_known_[2];
    float last_x_[2], last_y_[2], last_radius_[2];
    int result_[2];
    unsigned int tracking_[2];
    unsigned long long status_[2];
    unsigned int count_[2];
    unsigned int total_lost_ms_, longest_ms_, edge_count_, short_count_;
};

} // namespace ps3
} // namespace movebeat

#endif
