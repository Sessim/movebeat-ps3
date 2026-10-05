#include "demo_options.hpp"

#include <stdio.h>
#include <stdlib.h>
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
        else if (strcmp(line, "menu_test=1") == 0)
            options.menu_test = true;
        else if (strcmp(line, "playlist_test=1") == 0)
            options.playlist_test = true;
        else if (strncmp(line, "playlist_shelf=", 15) == 0)
            options.playlist_shelf = static_cast<unsigned int>(strtoul(line + 15, 0, 10));
        else if (strncmp(line, "level_id=", 9) == 0 &&
                 strlen(line + 9) <= 64)
            options.level_id = line + 9;
        else if (strncmp(line, "chart_index=", 12) == 0)
            parseDemoChartIndex(line + 12, options.chart_index);
        else if (strncmp(line, "capture_at_ms=", 14) == 0) {
            unsigned int parsed = 0;
            bool valid = line[14] != '\0';
            for (const char* digit = line + 14; *digit && valid; ++digit) {
                valid = *digit >= '0' && *digit <= '9' && parsed <= 600000u;
                if (valid) parsed = parsed * 10u + static_cast<unsigned int>(*digit - '0');
            }
            if (valid && parsed <= 600000u) options.capture_at_ms = parsed;
        }
    }
    fclose(file);
    return options;
}

RenderOptions readRenderOptions() {
    RenderOptions options;
    FILE* file = 0;
#if defined(_MSC_VER)
    fopen_s(&file, kRendererPath, "rb");
#else
    file = fopen(kRendererPath, "rb");
#endif
    if (!file) return options;
    char line[64] = {};
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "renderer=software") == 0) options.software = true;
        else if (strcmp(line, "effects=off") == 0) options.effects = false;
        else if (strcmp(line, "hand_colors=on") == 0) options.hand_colors = true;
        else if (strcmp(line, "hand_colors=off") == 0) options.hand_colors = false;
        else if (strcmp(line, "background=off") == 0) options.background = false;
        else if (strncmp(line, "camera_exposure=", 16) == 0)
            options.camera_exposure = atoi(line + 16);
        else if (strncmp(line, "camera_quality=", 15) == 0)
            options.camera_quality = static_cast<float>(atof(line + 15));
    }
    fclose(file);
    return options;
}

} // namespace ps3
} // namespace movebeat
