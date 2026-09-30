#ifndef MOVEBEAT_CHART_HPP
#define MOVEBEAT_CHART_HPP

#include <istream>
#include <string>
#include <vector>

#include "movebeat/types.hpp"

namespace movebeat {

enum NoteKind {
    NOTE_CUT,
    NOTE_BOMB,
    NOTE_CHAIN
};

struct Note {
    unsigned int time_ms;
    int lane;
    int row;
    Hand hand;
    CutDirection direction;
    float angle_offset_degrees;
    NoteKind kind;

    Note()
        : time_ms(0), lane(0), row(0), hand(HAND_LEFT), direction(CUT_ANY),
          angle_offset_degrees(0.0f), kind(NOTE_CUT) {}
};

struct Obstacle {
    unsigned int time_ms;
    unsigned int duration_ms;
    int lane;
    int width;
    int row;
    int height;

    Obstacle()
        : time_ms(0), duration_ms(0), lane(0), width(1), row(0), height(1) {}
};

struct Arc {
    unsigned int head_time_ms;
    unsigned int tail_time_ms;
    int head_lane;
    int head_row;
    CutDirection head_direction;
    float head_control_point_multiplier;
    int tail_lane;
    int tail_row;
    CutDirection tail_direction;
    float tail_control_point_multiplier;
    int mid_anchor_mode;
    Hand hand;

    Arc()
        : head_time_ms(0), tail_time_ms(0), head_lane(0), head_row(0),
          head_direction(CUT_ANY), head_control_point_multiplier(1.0f),
          tail_lane(0), tail_row(0), tail_direction(CUT_ANY),
          tail_control_point_multiplier(1.0f), mid_anchor_mode(0),
          hand(HAND_LEFT) {}
};

struct Chart {
    std::vector<Note> notes;
    std::vector<Obstacle> obstacles;
    std::vector<Arc> arcs;
};

bool loadChart(std::istream& input, Chart& chart, std::string& error);
bool loadChartFile(const std::string& path, Chart& chart, std::string& error);

} // namespace movebeat

#endif
