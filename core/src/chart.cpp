#include "movebeat/chart.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace movebeat {
namespace {

std::string trim(const std::string& value) {
    std::string::size_type first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::string::size_type last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

bool parseUnsigned(const std::string& text, unsigned int& value) {
    if (text.empty() || text[0] == '-') return false;
    std::istringstream stream(text);
    char trailing = 0;
    stream >> value;
    return stream && !(stream >> trailing);
}

bool parseInt(const std::string& text, int& value) {
    std::istringstream stream(text);
    char trailing = 0;
    stream >> value;
    return stream && !(stream >> trailing);
}

bool parseFloat(const std::string& text, float& value) {
    std::istringstream stream(text);
    char trailing = 0;
    stream >> value;
    return stream && !(stream >> trailing) && std::isfinite(value);
}

bool parseDirectionCode(int code, CutDirection& direction) {
    if (code < 0 || code > 8) return false;
    direction = static_cast<CutDirection>(code);
    return true;
}

bool noteEarlier(const Note& a, const Note& b) {
    return a.time_ms < b.time_ms;
}

bool obstacleEarlier(const Obstacle& a, const Obstacle& b) {
    return a.time_ms < b.time_ms;
}

bool arcEarlier(const Arc& a, const Arc& b) {
    return a.head_time_ms < b.head_time_ms;
}

} // namespace

bool loadChart(std::istream& input, Chart& chart, std::string& error) {
    Chart parsed;
    std::string line;
    unsigned int line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::vector<std::string> fields;
        std::istringstream row_stream(line);
        std::string field;
        while (std::getline(row_stream, field, ',')) {
            fields.push_back(trim(field));
        }
        if (!fields.empty() && fields[0] == "W") {
            if (fields.size() != 7) {
                std::ostringstream message;
                message << "line " << line_number
                        << ": wall expects W,time,duration,lane,width,row,height";
                error = message.str();
                return false;
            }
            Obstacle obstacle;
            if (!parseUnsigned(fields[1], obstacle.time_ms) ||
                !parseUnsigned(fields[2], obstacle.duration_ms) ||
                !parseInt(fields[3], obstacle.lane) ||
                !parseInt(fields[4], obstacle.width) ||
                !parseInt(fields[5], obstacle.row) ||
                !parseInt(fields[6], obstacle.height)) {
                std::ostringstream message;
                message << "line " << line_number << ": invalid wall numeric value";
                error = message.str();
                return false;
            }
            if (obstacle.duration_ms == 0 || obstacle.lane < 0 ||
                obstacle.width < 1 || obstacle.lane + obstacle.width > 4 ||
                obstacle.row < 0 || obstacle.height < 1 ||
                obstacle.row + obstacle.height > 3) {
                std::ostringstream message;
                message << "line " << line_number << ": wall is outside the 4x3 grid";
                error = message.str();
                return false;
            }
            parsed.obstacles.push_back(obstacle);
            continue;
        }
        if (!fields.empty() && fields[0] == "C") {
            if (fields.size() != 5) {
                std::ostringstream message;
                message << "line " << line_number
                        << ": chain expects C,time,lane,row,hand";
                error = message.str();
                return false;
            }
            Note note;
            note.kind = NOTE_CHAIN;
            note.direction = CUT_ANY;
            if (!parseUnsigned(fields[1], note.time_ms) ||
                !parseInt(fields[2], note.lane) ||
                !parseInt(fields[3], note.row)) {
                std::ostringstream message;
                message << "line " << line_number << ": invalid chain numeric value";
                error = message.str();
                return false;
            }
            if (note.lane < 0 || note.lane > 3 || note.row < 0 || note.row > 2) {
                std::ostringstream message;
                message << "line " << line_number << ": chain is outside the 4x3 grid";
                error = message.str();
                return false;
            }
            if (fields[4] == "L") note.hand = HAND_LEFT;
            else if (fields[4] == "R") note.hand = HAND_RIGHT;
            else {
                std::ostringstream message;
                message << "line " << line_number << ": chain hand must be L or R";
                error = message.str();
                return false;
            }
            parsed.notes.push_back(note);
            continue;
        }
        if (!fields.empty() && fields[0] == "A") {
            if (fields.size() != 8 && fields.size() != 13) {
                std::ostringstream message;
                message << "line " << line_number
                        << ": arc expects A,head,tail,head_lane,head_row,"
                           "tail_lane,tail_row,hand (legacy) or control-point fields";
                error = message.str();
                return false;
            }
            Arc arc;
            int head_direction = 8;
            int tail_direction = 8;
            if (!parseUnsigned(fields[1], arc.head_time_ms) ||
                !parseUnsigned(fields[2], arc.tail_time_ms) ||
                !parseInt(fields[3], arc.head_lane) ||
                !parseInt(fields[4], arc.head_row)) {
                std::ostringstream message;
                message << "line " << line_number << ": invalid arc numeric value";
                error = message.str();
                return false;
            }
            const std::size_t tail_lane_field = fields.size() == 8 ? 5u : 7u;
            const std::size_t tail_row_field = fields.size() == 8 ? 6u : 8u;
            const std::size_t hand_field = fields.size() == 8 ? 7u : 12u;
            if (!parseInt(fields[tail_lane_field], arc.tail_lane) ||
                !parseInt(fields[tail_row_field], arc.tail_row)) {
                std::ostringstream message;
                message << "line " << line_number << ": invalid arc numeric value";
                error = message.str();
                return false;
            }
            if (fields.size() == 13 &&
                (!parseInt(fields[5], head_direction) ||
                 !parseFloat(fields[6], arc.head_control_point_multiplier) ||
                 !parseInt(fields[9], tail_direction) ||
                 !parseFloat(fields[10], arc.tail_control_point_multiplier) ||
                 !parseInt(fields[11], arc.mid_anchor_mode))) {
                std::ostringstream message;
                message << "line " << line_number << ": invalid arc curve value";
                error = message.str();
                return false;
            }
            if (!parseDirectionCode(head_direction, arc.head_direction) ||
                !parseDirectionCode(tail_direction, arc.tail_direction)) {
                std::ostringstream message;
                message << "line " << line_number << ": invalid arc direction";
                error = message.str();
                return false;
            }
            if (arc.tail_time_ms < arc.head_time_ms ||
                arc.head_lane < 0 || arc.head_lane > 3 ||
                arc.tail_lane < 0 || arc.tail_lane > 3 ||
                arc.head_row < 0 || arc.head_row > 2 ||
                arc.tail_row < 0 || arc.tail_row > 2 ||
                arc.head_control_point_multiplier < 0.0f ||
                arc.head_control_point_multiplier > 10.0f ||
                arc.tail_control_point_multiplier < 0.0f ||
                arc.tail_control_point_multiplier > 10.0f ||
                arc.mid_anchor_mode < 0 || arc.mid_anchor_mode > 2) {
                std::ostringstream message;
                message << "line " << line_number << ": invalid arc bounds";
                error = message.str();
                return false;
            }
            if (fields[hand_field] == "L") arc.hand = HAND_LEFT;
            else if (fields[hand_field] == "R") arc.hand = HAND_RIGHT;
            else {
                std::ostringstream message;
                message << "line " << line_number << ": arc hand must be L or R";
                error = message.str();
                return false;
            }
            parsed.arcs.push_back(arc);
            continue;
        }
        if (fields.size() != 5 && fields.size() != 6) {
            std::ostringstream message;
            message << "line " << line_number
                    << ": expected 5 or 6 comma-separated note fields";
            error = message.str();
            return false;
        }

        Note note;
        if (!parseUnsigned(fields[0], note.time_ms) ||
            !parseInt(fields[1], note.lane) ||
            !parseInt(fields[2], note.row)) {
            std::ostringstream message;
            message << "line " << line_number << ": invalid numeric value";
            error = message.str();
            return false;
        }
        if (fields.size() == 6 &&
            !parseFloat(fields[5], note.angle_offset_degrees)) {
            std::ostringstream message;
            message << "line " << line_number
                    << ": invalid note angle offset";
            error = message.str();
            return false;
        }
        if (note.lane < 0 || note.lane > 3 || note.row < 0 || note.row > 2) {
            std::ostringstream message;
            message << "line " << line_number << ": lane must be 0..3 and row 0..2";
            error = message.str();
            return false;
        }

        if (fields[3] == "L") note.hand = HAND_LEFT;
        else if (fields[3] == "R") note.hand = HAND_RIGHT;
        else if (fields[3] == "B") {
            note.hand = HAND_NONE;
            note.kind = NOTE_BOMB;
        }
        else {
            std::ostringstream message;
            message << "line " << line_number << ": hand must be L, R or B";
            error = message.str();
            return false;
        }

        if (fields[4] == "U") note.direction = CUT_UP;
        else if (fields[4] == "D") note.direction = CUT_DOWN;
        else if (fields[4] == "L") note.direction = CUT_LEFT;
        else if (fields[4] == "R") note.direction = CUT_RIGHT;
        else if (fields[4] == "UL") note.direction = CUT_UP_LEFT;
        else if (fields[4] == "UR") note.direction = CUT_UP_RIGHT;
        else if (fields[4] == "DL") note.direction = CUT_DOWN_LEFT;
        else if (fields[4] == "DR") note.direction = CUT_DOWN_RIGHT;
        else if (fields[4] == "A") note.direction = CUT_ANY;
        else {
            std::ostringstream message;
            message << "line " << line_number
                    << ": direction must be U, D, L, R, UL, UR, DL, DR or A";
            error = message.str();
            return false;
        }
        if (note.kind == NOTE_BOMB) note.direction = CUT_ANY;
        parsed.notes.push_back(note);
    }

    std::stable_sort(parsed.notes.begin(), parsed.notes.end(), noteEarlier);
    std::stable_sort(parsed.obstacles.begin(), parsed.obstacles.end(), obstacleEarlier);
    std::stable_sort(parsed.arcs.begin(), parsed.arcs.end(), arcEarlier);
    chart = parsed;
    error.clear();
    return true;
}

bool loadChartFile(const std::string& path, Chart& chart, std::string& error) {
    std::ifstream file(path.c_str());
    if (!file) {
        error = "could not open chart: " + path;
        return false;
    }
    return loadChart(file, chart, error);
}

} // namespace movebeat
