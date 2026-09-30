#include "movebeat/settings.hpp"

#include <fstream>
#include <map>
#include <sstream>

namespace movebeat {

bool loadSettings(std::istream& input, Settings& settings, std::string& error) {
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::string::size_type separator = line.find('=');
        if (separator == std::string::npos) continue;
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }
    if (input.bad()) {
        error = "could not read settings";
        return false;
    }
    if (values["format"] != "1") {
        error = "unsupported settings format";
        return false;
    }
    std::istringstream offset(values["timing_offset_ms"]);
    int parsed_offset = 0;
    char trailing = 0;
    if (!(offset >> parsed_offset) || (offset >> trailing) ||
        parsed_offset < -500 || parsed_offset > 500) {
        error = "timing_offset_ms must be -500..500";
        return false;
    }
    Settings parsed;
    parsed.timing_offset_ms = parsed_offset;
    const std::map<std::string, std::string>::const_iterator blade_value =
        values.find("blade_length_cm");
    if (blade_value != values.end()) {
        std::istringstream blade(blade_value->second);
        int parsed_blade = 0;
        trailing = 0;
        if (!(blade >> parsed_blade) || (blade >> trailing) ||
            parsed_blade < Settings::MIN_BLADE_LENGTH_CM ||
            parsed_blade > Settings::MAX_BLADE_LENGTH_CM) {
            error = "blade_length_cm must be 50..140";
            return false;
        }
        parsed.blade_length_cm = parsed_blade;
    }
    const std::map<std::string, std::string>::const_iterator hand_value =
        values.find("invert_hands");
    if (hand_value != values.end()) {
        if (hand_value->second == "1") parsed.invert_hands = true;
        else if (hand_value->second == "0") parsed.invert_hands = false;
        else {
            error = "invert_hands must be 0 or 1";
            return false;
        }
    }
    settings = parsed;
    error.clear();
    return true;
}

bool loadSettingsFile(const std::string& path, Settings& settings, std::string& error) {
    std::ifstream input(path.c_str());
    if (!input) {
        settings = Settings();
        error.clear();
        return true;
    }
    return loadSettings(input, settings, error);
}

bool saveSettingsFile(const std::string& path, const Settings& settings,
                      std::string& error) {
    if (settings.timing_offset_ms < -500 || settings.timing_offset_ms > 500) {
        error = "timing_offset_ms must be -500..500";
        return false;
    }
    if (settings.blade_length_cm < Settings::MIN_BLADE_LENGTH_CM ||
        settings.blade_length_cm > Settings::MAX_BLADE_LENGTH_CM) {
        error = "blade_length_cm must be 50..140";
        return false;
    }
    std::ofstream output(path.c_str(), std::ios::out | std::ios::trunc);
    if (!output) {
        error = "could not open settings file";
        return false;
    }
    output << "format=1\n"
           << "timing_offset_ms=" << settings.timing_offset_ms << '\n'
           << "blade_length_cm=" << settings.blade_length_cm << '\n'
           << "invert_hands=" << (settings.invert_hands ? 1 : 0) << '\n';
    if (!output) {
        error = "could not write settings file";
        return false;
    }
    error.clear();
    return true;
}

} // namespace movebeat
