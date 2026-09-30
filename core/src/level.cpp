#include "movebeat/level.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace movebeat {
namespace {

std::string trim(const std::string& value) {
    std::string::size_type first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::string::size_type last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

bool parseUnsigned(const std::string& text, unsigned int& result) {
    if (text.empty() || text[0] == '-') return false;
    std::istringstream stream(text);
    char trailing = 0;
    stream >> result;
    return stream && !(stream >> trailing);
}

bool parseFloat(const std::string& text, float& result) {
    if (text.empty()) return false;
    std::istringstream stream(text);
    char trailing = 0;
    stream >> result;
    return stream && !(stream >> trailing) && std::isfinite(result);
}

bool safeLeafName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
        name.find('/') == std::string::npos &&
        name.find('\\') == std::string::npos &&
        name.find(':') == std::string::npos;
}

std::string joinPath(const std::string& directory, const std::string& leaf) {
    if (directory.empty()) return leaf;
    const char last = directory[directory.size() - 1];
    return directory + ((last == '/' || last == '\\') ? "" : "/") + leaf;
}

bool require(const std::map<std::string, std::string>& values,
             const std::string& key, std::string& result, std::string& error) {
    const std::map<std::string, std::string>::const_iterator found = values.find(key);
    if (found == values.end() || found->second.empty()) {
        error = "level.ini: missing " + key;
        return false;
    }
    result = found->second;
    return true;
}

} // namespace

bool loadLevelManifest(std::istream& input, LevelManifest& level, std::string& error) {
    std::map<std::string, std::string> values;
    std::string line;
    unsigned int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::string::size_type separator = line.find('=');
        if (separator == std::string::npos) {
            std::ostringstream message;
            message << "level.ini line " << line_number << ": expected key=value";
            error = message.str();
            return false;
        }
        const std::string key = trim(line.substr(0, separator));
        if (key.empty()) {
            error = "level.ini: empty key";
            return false;
        }
        values[key] = trim(line.substr(separator + 1));
    }

    LevelManifest parsed;
    std::string format;
    std::string bpm_text;
    std::string count_text;
    if (!require(values, "format", format, error) || format != "1") {
        if (error.empty()) error = "level.ini: unsupported format";
        else if (format != "1") error = "level.ini: unsupported format";
        return false;
    }
    if (!require(values, "title", parsed.title, error) ||
        !require(values, "audio", parsed.audio, error) ||
        !require(values, "bpm", bpm_text, error) ||
        !require(values, "chart_count", count_text, error)) return false;
    if (!safeLeafName(parsed.audio)) {
        error = "level.ini: unsafe audio filename";
        return false;
    }
    std::istringstream bpm_stream(bpm_text);
    char bpm_trailing = 0;
    bpm_stream >> parsed.bpm;
    if (!bpm_stream || (bpm_stream >> bpm_trailing) || parsed.bpm <= 0.0f) {
        error = "level.ini: invalid bpm";
        return false;
    }
    unsigned int chart_count = 0;
    if (!parseUnsigned(count_text, chart_count) || chart_count == 0 || chart_count > 64) {
        error = "level.ini: chart_count must be 1..64";
        return false;
    }

    parsed.source = values["source"];
    parsed.source_key = values["source_key"];
    parsed.song_author = values["song_author"];
    parsed.mapper = values["mapper"];
    parsed.cover = values["cover"];
    if (!parsed.cover.empty() && !safeLeafName(parsed.cover)) {
        error = "level.ini: unsafe cover filename";
        return false;
    }
    for (unsigned int index = 0; index < chart_count; ++index) {
        std::ostringstream prefix;
        prefix << "chart" << index << "_";
        LevelChart chart;
        std::string notes_text;
        std::string obstacles_text;
        std::string skipped_text;
        if (!require(values, prefix.str() + "characteristic", chart.characteristic, error) ||
            !require(values, prefix.str() + "difficulty", chart.difficulty, error) ||
            !require(values, prefix.str() + "file", chart.file, error) ||
            !require(values, prefix.str() + "notes", notes_text, error) ||
            !require(values, prefix.str() + "skipped", skipped_text, error)) return false;
        if (!safeLeafName(chart.file) ||
            !parseUnsigned(notes_text, chart.notes) ||
            !parseUnsigned(skipped_text, chart.skipped)) {
            error = "level.ini: invalid chart entry";
            return false;
        }
        const std::map<std::string, std::string>::const_iterator obstacles_value =
            values.find(prefix.str() + "obstacles");
        if (obstacles_value != values.end() &&
            !parseUnsigned(obstacles_value->second, chart.obstacles)) {
            error = "level.ini: invalid chart obstacle count";
            return false;
        }
        const std::map<std::string, std::string>::const_iterator chains_value =
            values.find(prefix.str() + "chains");
        if (chains_value != values.end() &&
            !parseUnsigned(chains_value->second, chart.chains)) {
            error = "level.ini: invalid chart chain count";
            return false;
        }
        const std::map<std::string, std::string>::const_iterator arcs_value =
            values.find(prefix.str() + "arcs");
        if (arcs_value != values.end() &&
            !parseUnsigned(arcs_value->second, chart.arcs)) {
            error = "level.ini: invalid chart arc count";
            return false;
        }
        const std::map<std::string, std::string>::const_iterator njs_value =
            values.find(prefix.str() + "njs");
        const std::map<std::string, std::string>::const_iterator offset_value =
            values.find(prefix.str() + "start_beat_offset");
        if (njs_value != values.end()) {
            if (!parseFloat(njs_value->second, chart.note_jump_speed) ||
                chart.note_jump_speed < 0.1f || chart.note_jump_speed > 40.0f ||
                (offset_value != values.end() &&
                 !parseFloat(offset_value->second, chart.start_beat_offset)) ||
                chart.start_beat_offset < -16.0f ||
                chart.start_beat_offset > 16.0f) {
                error = "level.ini: invalid chart note jump metadata";
                return false;
            }
        } else if (offset_value != values.end()) {
            error = "level.ini: start beat offset requires note jump speed";
            return false;
        }
        parsed.charts.push_back(chart);
    }
    level = parsed;
    error.clear();
    return true;
}

bool loadLevelDirectory(const std::string& directory, LevelManifest& level,
                        std::string& error) {
    std::ifstream manifest(joinPath(directory, "level.ini").c_str());
    if (!manifest) {
        error = "could not open level.ini in " + directory;
        return false;
    }
    return loadLevelManifest(manifest, level, error);
}

bool loadLevelChart(const std::string& directory, const LevelManifest& level,
                    std::size_t chart_index, Chart& chart, std::string& error) {
    if (chart_index >= level.charts.size()) {
        error = "chart index is out of range";
        return false;
    }
    return loadChartFile(joinPath(directory, level.charts[chart_index].file), chart, error);
}

} // namespace movebeat
