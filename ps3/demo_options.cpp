#include "demo_options.hpp"

#include <stdio.h>
#include <string.h>

#include "app_paths.hpp"

namespace movebeat {
namespace ps3 {
namespace {

bool parseDemoChartIndex(const char* text, unsigned int& index) {
    if (!text || !*text) return false;
    unsigned int parsed = 0;
    for (const char* digit = text; *digit; ++digit) {
        if (*digit < '0' || *digit > '9') return false;
        parsed = parsed * 10u + static_cast<unsigned int>(*digit - '0');
        if (parsed > 63u) return false;
    }
    index = parsed;
    return true;
}

} // namespace

EmulatorDemoOptions readEmulatorDemoOptions() {
    EmulatorDemoOptions options;
    FILE* file = 0;
#if defined(_MSC_VER)
    fopen_s(&file, kDemoModePath, "rb");
#else
    file = fopen(kDemoModePath, "rb");
#endif
    if (!file) return options;
    char line[128] = {};
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "enabled=1") == 0) options.enabled = true;
        else if (strcmp(line, "automatic_cuts=0") == 0)
            options.automatic_cuts = false;
        else if (strcmp(line, "tracking_loss_test=1") == 0)
            options.tracking_loss_test = true;
        else if (strncmp(line, "level_id=", 9) == 0 &&
                 strlen(line + 9) <= 64)
            options.level_id = line + 9;
        else if (strncmp(line, "chart_index=", 12) == 0)
            parseDemoChartIndex(line + 12, options.chart_index);
    }
    fclose(file);
    return options;
}

} // namespace ps3
} // namespace movebeat
