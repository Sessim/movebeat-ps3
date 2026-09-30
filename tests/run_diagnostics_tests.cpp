#include "run_diagnostics.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main() {
    const char* const path = "movebeat_run_diagnostics_test.ini";
    movebeat::ps3::RunStatus status;
    status.hand_slots_swapped = 2;
    status.hand_assignment_established = 3;
    status.invert_hands_setting = 4;
    status.hue_values_known = 5;
    status.hand_hues_applied = 6;
    status.hue_assignment_pending = 7;
    status.gem_slot0_hue = 204;
    status.gem_slot1_hue = 352;
    status.hue_error_code = 0x12345678u;
    status.tracking_paused = 8;
    status.tracking_recalibrating = 9;

    if (!movebeat::ps3::writeRunStatus(path, status)) {
        std::cerr << "FAIL: run status write failed\n";
        return 1;
    }
    std::ifstream input(path, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    input.close();
    std::remove(path);
    contents.erase(std::remove(contents.begin(), contents.end(), '\r'),
                   contents.end());

    const char* const expected[] = {
        "hand_slots_swapped=2\n",
        "hand_assignment_established=3\n",
        "invert_hands_setting=4\n",
        "hue_values_known=5\n",
        "hand_hues_applied=6\n",
        "hue_assignment_pending=7\n",
        "gem_slot0_hue=204\n",
        "gem_slot1_hue=352\n",
        "hue_error_code=0x12345678\n",
        "tracking_paused=8\n",
        "tracking_recalibrating=9\n",
    };
    for (const char* field : expected) {
        if (contents.find(field) == std::string::npos) {
            std::cerr << "FAIL: missing or misordered diagnostic field: "
                      << field << "\nActual status:\n" << contents;
            return 1;
        }
    }
    std::puts("Run diagnostics fields passed.");
    return 0;
}
