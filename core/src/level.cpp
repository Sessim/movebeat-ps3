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

// Reads `key=value` lines; blank lines and # comments are skipped.
bool readKeyValues(std::istream& input, const char* file_name,
                   std::map<std::string, std::string>& values, std::string& error) {
    std::string line;
    unsigned int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::string::size_type separator = line.find('=');
        if (separator == std::string::npos) {
            std::ostringstream message;
            message << file_name << " line " << line_number << ": expected key=value";
            error = message.str();
            return false;
        }
        const std::string key = trim(line.substr(0, separator));
        if (key.empty()) {
            error = std::string(file_name) + ": empty key";
            return false;
        }
        values[key] = trim(line.substr(separator + 1));
    }
    return true;
}

std::string lowerAscii(const std::string& value) {
    std::string result(value);
    for (std::size_t index = 0; index < result.size(); ++index)
        if (result[index] >= 'A' && result[index] <= 'Z')
            result[index] = static_cast<char>(result[index] - 'A' + 'a');
    return result;
}

bool hexHash(const std::string& value) {
    if (value.size() != 40) return false;
    for (std::size_t index = 0; index < value.size(); ++index)
        if (!std::isxdigit(static_cast<unsigned char>(value[index]))) return false;
    return true;
}

} // namespace

bool loadLevelManifest(std::istream& input, LevelManifest& level, std::string& error) {
    std::map<std::string, std::string> values;
    if (!readKeyValues(input, "level.ini", values, error)) return false;

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
    // An unusable hash only loses playlist matching by hash, not the level.
    if (hexHash(values["source_hash"])) parsed.source_hash = lowerAscii(values["source_hash"]);
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

bool loadPlaylistManifest(std::istream& input, PlaylistManifest& playlist,
                          std::string& error) {
    std::map<std::string, std::string> values;
    if (!readKeyValues(input, "playlist.ini", values, error)) return false;
    if (values["format"] != "1") {
        error = "playlist.ini: unsupported format";
        return false;
    }
    PlaylistManifest parsed;
    parsed.title = values["title"];
    parsed.author = values["author"];
    if (parsed.title.empty()) {
        error = "playlist.ini: missing title";
        return false;
    }
    unsigned int count = 0;
    if (!parseUnsigned(values["song_count"], count) || count == 0 ||
        count > kMaxPlaylistSongs) {
        error = "playlist.ini: song_count must be 1..256";
        return false;
    }
    for (unsigned int index = 0; index < count; ++index) {
        std::ostringstream prefix;
        prefix << "song" << index << "_";
        PlaylistSong song;
        const std::string hash = values[prefix.str() + "hash"];
        if (!hash.empty()) {
            if (!hexHash(hash)) {
                error = "playlist.ini: invalid " + prefix.str() + "hash";
                return false;
            }
            song.hash = lowerAscii(hash);
        }
        song.key = values[prefix.str() + "key"];
        if (song.hash.empty() && song.key.empty()) {
            error = "playlist.ini: " + prefix.str() + " needs a hash or a key";
            return false;
        }
        song.title = values[prefix.str() + "title"];
        song.characteristic = values[prefix.str() + "characteristic"];
        song.difficulty = values[prefix.str() + "difficulty"];
        parsed.songs.push_back(song);
    }
    playlist = parsed;
    error.clear();
    return true;
}

bool loadPlaylistDirectory(const std::string& directory,
                           PlaylistManifest& playlist, std::string& error) {
    std::ifstream manifest(joinPath(directory, "playlist.ini").c_str());
    if (!manifest) {
        error = "could not open playlist.ini in " + directory;
        return false;
    }
    return loadPlaylistManifest(manifest, playlist, error);
}

bool playlistSongMatches(const PlaylistSong& song, const LevelManifest& level) {
    if (!song.hash.empty() && !level.source_hash.empty())
        return song.hash == level.source_hash;
    return !song.key.empty() && !level.source_key.empty() &&
           lowerAscii(song.key) == lowerAscii(level.source_key);
}

} // namespace movebeat
