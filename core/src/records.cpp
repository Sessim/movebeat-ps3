#include "movebeat/records.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace movebeat {
namespace {

bool validKey(const std::string& key) {
    if (key.size() != 16) return false;
    for (std::size_t index = 0; index < key.size(); ++index) {
        const char value = key[index];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f'))) return false;
    }
    return true;
}

} // namespace

std::string makeScoreKey(const std::string& level_identity,
                         const std::string& chart_identity) {
    // Stable FNV-1a key keeps arbitrary UTF-8 titles and paths out of the log.
    unsigned long long hash = 14695981039346656037ull;
    const std::string combined = level_identity + "\n" + chart_identity;
    for (std::size_t index = 0; index < combined.size(); ++index) {
        hash ^= static_cast<unsigned char>(combined[index]);
        hash *= 1099511628211ull;
    }
    std::ostringstream key;
    key << std::hex << std::setfill('0') << std::setw(16) << hash;
    return key.str();
}

bool readBestScore(std::istream& input, const std::string& key,
                   HighScore& result, std::string& error) {
    if (!validKey(key)) {
        error = "invalid score key";
        return false;
    }
    HighScore best;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string row_key;
        HighScore candidate;
        char trailing = 0;
        if (!(row >> row_key >> candidate.score >> candidate.max_combo) ||
            (row >> trailing) || !validKey(row_key)) continue;
        if (row_key == key && candidate.score >= best.score) best = candidate;
    }
    if (input.bad()) {
        error = "could not read score log";
        return false;
    }
    result = best;
    error.clear();
    return true;
}

bool loadBestScoreFile(const std::string& path, const std::string& key,
                       HighScore& result, std::string& error) {
    std::ifstream input(path.c_str());
    if (!input) {
        result = HighScore();
        error.clear();
        return true;
    }
    return readBestScore(input, key, result, error);
}

void readAllBestScores(std::istream& input, std::map<std::string, HighScore>& result) {
    result.clear();
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string row_key;
        HighScore candidate;
        char trailing = 0;
        if (!(row >> row_key >> candidate.score >> candidate.max_combo) ||
            (row >> trailing) || !validKey(row_key)) continue;
        HighScore& best = result[row_key];
        if (candidate.score >= best.score) best = candidate;
    }
}

void loadAllBestScoresFile(const std::string& path,
                           std::map<std::string, HighScore>& result) {
    std::ifstream input(path.c_str());
    if (!input) {
        result.clear();
        return;
    }
    readAllBestScores(input, result);
}

bool appendScoreFile(const std::string& path, const std::string& key,
                     const HighScore& score, std::string& error) {
    if (!validKey(key)) {
        error = "invalid score key";
        return false;
    }
    std::ofstream output(path.c_str(), std::ios::out | std::ios::app);
    if (!output) {
        error = "could not open score log";
        return false;
    }
    output << key << ' ' << score.score << ' ' << score.max_combo << '\n';
    if (!output) {
        error = "could not write score log";
        return false;
    }
    error.clear();
    return true;
}

} // namespace movebeat
