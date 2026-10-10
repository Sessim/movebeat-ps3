#ifndef MOVEBEAT_PS3_LIBRARY_SHELF_HPP
#define MOVEBEAT_PS3_LIBRARY_SHELF_HPP

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "menu_motion.hpp"
#include "movebeat/level.hpp"

namespace movebeat {
namespace ps3 {

// Library positions (indices into the sorted track list) of the songs a
// playlist names, in playlist order. Songs not in the library are skipped;
// `song_indices` receives the playlist song index of every track found.
inline std::vector<std::size_t> resolvePlaylistTracks(
        const PlaylistManifest& playlist,
        const std::vector<const LevelManifest*>& levels,
        std::vector<std::size_t>& song_indices) {
    std::vector<std::size_t> tracks;
    song_indices.clear();
    for (std::size_t song = 0; song < playlist.songs.size(); ++song) {
        for (std::size_t level = 0; level < levels.size(); ++level) {
            if (!levels[level] || !playlistSongMatches(playlist.songs[song], *levels[level]))
                continue;
            tracks.push_back(level);
            song_indices.push_back(song);
            break;
        }
    }
    return tracks;
}

inline bool sameAsciiFolded(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        char a = left[index], b = right[index];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// The chart to play for a playlist song: the difficulty the playlist names
// when the level has it, otherwise the chart closest to `preferred_rank`
// (difficultyRank scale; ties go to the easier one). Standard charts win
// over other characteristics unless the playlist names one. A negative
// rank with no suggestion keeps the first chart.
inline std::size_t choosePlaylistChart(const PlaylistSong& song,
                                       const LevelManifest& level,
                                       int preferred_rank) {
    const std::vector<LevelChart>& charts = level.charts;
    if (charts.empty()) return 0;
    const std::string characteristic =
        song.characteristic.empty() ? std::string("Standard") : song.characteristic;
    if (!song.difficulty.empty()) {
        for (std::size_t index = 0; index < charts.size(); ++index)
            if (sameAsciiFolded(charts[index].characteristic, characteristic) &&
                difficultyRank(charts[index].difficulty) == difficultyRank(song.difficulty))
                return index;
        if (preferred_rank < 0) preferred_rank = difficultyRank(song.difficulty);
    }
    if (preferred_rank < 0) return 0;
    std::size_t best = charts.size();
    int best_distance = 0;
    bool best_matches = false;
    for (std::size_t index = 0; index < charts.size(); ++index) {
        const bool matches = sameAsciiFolded(charts[index].characteristic, characteristic);
        const int rank = difficultyRank(charts[index].difficulty);
        const int distance = std::abs(rank - preferred_rank) * 2 +
                             (rank > preferred_rank ? 1 : 0);
        if (best == charts.size() || (matches && !best_matches) ||
            (matches == best_matches && distance < best_distance)) {
            best = index;
            best_distance = distance;
            best_matches = matches;
        }
    }
    return best;
}

// One finished song of a playlist run.
struct PlaylistSongResult {
    std::size_t level;
    std::size_t chart;
    unsigned int score;
    unsigned int hits;
    unsigned int misses;
    unsigned int max_combo;
    bool failed;

    PlaylistSongResult() : level(0), chart(0), score(0), hits(0), misses(0),
                           max_combo(0), failed(false) {}
};

// Playing a playlist through: from the song it was started on to the end,
// one after another, with the results of every song kept for the summary.
class PlaylistRun {
public:
    PlaylistRun() : active_(false), playlist_(0), position_(0), first_(0),
                    preferred_rank_(-1) {}

    // `tracks` are library indices in playlist order; `start` is a
    // position in it.
    void begin(std::size_t playlist, const std::vector<std::size_t>& tracks,
               std::size_t start, int preferred_rank) {
        active_ = start < tracks.size();
        playlist_ = playlist;
        tracks_ = tracks;
        position_ = start;
        first_ = start;
        preferred_rank_ = preferred_rank;
        results_.clear();
    }
    void stop() { active_ = false; }

    bool active() const { return active_; }
    std::size_t playlist() const { return playlist_; }
    // Position in the playlist of the song being played or just finished.
    std::size_t position() const { return position_; }
    std::size_t firstPosition() const { return first_; }
    std::size_t trackCount() const { return tracks_.size(); }
    std::size_t currentLevel() const { return active_ ? tracks_[position_] : 0; }
    int preferredRank() const { return preferred_rank_; }

    void record(const PlaylistSongResult& result) {
        if (active_) results_.push_back(result);
    }
    // Forgets the latest result (its song is played again).
    void dropLast() {
        if (!results_.empty()) results_.pop_back();
    }
    // A next song exists after the one just recorded.
    bool hasNext() const { return active_ && position_ + 1 < tracks_.size(); }
    // Moves to the next song; returns its library index.
    std::size_t advance() {
        if (hasNext()) ++position_;
        return tracks_[position_];
    }
    // Every song from the start position has been recorded.
    bool complete() const {
        return active_ && results_.size() == tracks_.size() - first_;
    }

    const std::vector<PlaylistSongResult>& results() const { return results_; }
    unsigned int totalScore() const {
        unsigned int total = 0;
        for (std::size_t index = 0; index < results_.size(); ++index) {
            const unsigned int score = results_[index].score;
            total = total > 0xffffffffu - score ? 0xffffffffu : total + score;
        }
        return total;
    }
    unsigned int totalHits() const {
        unsigned int total = 0;
        for (std::size_t index = 0; index < results_.size(); ++index)
            total += results_[index].hits;
        return total;
    }
    unsigned int totalMisses() const {
        unsigned int total = 0;
        for (std::size_t index = 0; index < results_.size(); ++index)
            total += results_[index].misses;
        return total;
    }

private:
    bool active_;
    std::size_t playlist_;
    std::vector<std::size_t> tracks_;
    std::size_t position_;
    std::size_t first_;
    int preferred_rank_;
    std::vector<PlaylistSongResult> results_;
};

} // namespace ps3
} // namespace movebeat

#endif
