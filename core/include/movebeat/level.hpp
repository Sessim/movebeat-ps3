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

} // namespace movebeat

#endif
