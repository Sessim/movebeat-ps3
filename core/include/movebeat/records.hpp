#ifndef MOVEBEAT_RECORDS_HPP
#define MOVEBEAT_RECORDS_HPP

#include <istream>
#include <string>

namespace movebeat {

struct HighScore {
    unsigned int score;
    unsigned int max_combo;

    HighScore() : score(0), max_combo(0) {}
};

std::string makeScoreKey(const std::string& level_identity,
                         const std::string& chart_identity);
bool readBestScore(std::istream& input, const std::string& key,
                   HighScore& result, std::string& error);
bool loadBestScoreFile(const std::string& path, const std::string& key,
                       HighScore& result, std::string& error);
bool appendScoreFile(const std::string& path, const std::string& key,
                     const HighScore& score, std::string& error);

} // namespace movebeat

#endif
