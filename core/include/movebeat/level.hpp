#ifndef MOVEBEAT_LEVEL_HPP
#define MOVEBEAT_LEVEL_HPP

#include <istream>
#include <string>
#include <vector>

#include "movebeat/chart.hpp"

namespace movebeat {

struct LevelChart {
    std::string characteristic;
    std::string difficulty;
    std::string file;
    unsigned int notes;
    unsigned int obstacles;
    unsigned int chains;
    unsigned int arcs;
    unsigned int skipped;
    float note_jump_speed;
    float start_beat_offset;

    LevelChart() : notes(0), obstacles(0), chains(0), arcs(0), skipped(0),
                   note_jump_speed(0.0f), start_beat_offset(0.0f) {}
};

struct LevelManifest {
    std::string source;
    std::string source_key;
    // Beat Saber content SHA-1 (lowercase hex), how playlists name a map.
    std::string source_hash;
    std::string title;
    std::string song_author;
    std::string mapper;
    std::string audio;
    std::string cover;
    float bpm;
    std::vector<LevelChart> charts;

    LevelManifest() : bpm(0.0f) {}
};

bool loadLevelManifest(std::istream& input, LevelManifest& level, std::string& error);
bool loadLevelDirectory(const std::string& directory, LevelManifest& level,
                        std::string& error);
bool loadLevelChart(const std::string& directory, const LevelManifest& level,
                    std::size_t chart_index, Chart& chart, std::string& error);

// One map of a playlist, named by content hash and/or BeatSaver key. The
// characteristic and difficulty are optional: what the playlist suggests.
struct PlaylistSong {
    std::string hash;
    std::string key;
    std::string title;
    std::string characteristic;
    std::string difficulty;
};

// playlist.ini of a playlist bundle (converted from a BeatSaver .bplist).
struct PlaylistManifest {
    std::string title;
    std::string author;
    std::vector<PlaylistSong> songs;
};

const unsigned int kMaxPlaylistSongs = 256;

bool loadPlaylistManifest(std::istream& input, PlaylistManifest& playlist,
                          std::string& error);
bool loadPlaylistDirectory(const std::string& directory,
                           PlaylistManifest& playlist, std::string& error);
// True when the song names this level: by hash (case-insensitive) when both
// have one, otherwise by BeatSaver key (case-insensitive).
bool playlistSongMatches(const PlaylistSong& song, const LevelManifest& level);

} // namespace movebeat

#endif
