#ifndef MOVEBEAT_SETTINGS_HPP
#define MOVEBEAT_SETTINGS_HPP

#include <istream>
#include <string>

namespace movebeat {

struct Settings {
    enum {
        MIN_BLADE_LENGTH_CM = 50,
        MAX_BLADE_LENGTH_CM = 140,
        DEFAULT_BLADE_LENGTH_CM = 85
    };

    int timing_offset_ms;
    int blade_length_cm;
    bool invert_hands;
    // On-screen language: "auto" (the PS3 system language), "en" or "ru".
    std::string language;

    Settings()
        : timing_offset_ms(0), blade_length_cm(DEFAULT_BLADE_LENGTH_CM),
          invert_hands(false), language("auto") {}
};

bool loadSettings(std::istream& input, Settings& settings, std::string& error);
bool loadSettingsFile(const std::string& path, Settings& settings, std::string& error);
bool saveSettingsFile(const std::string& path, const Settings& settings,
                      std::string& error);

} // namespace movebeat

#endif
