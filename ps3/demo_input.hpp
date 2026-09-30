#ifndef MOVEBEAT_PS3_DEMO_INPUT_HPP
#define MOVEBEAT_PS3_DEMO_INPUT_HPP

#include "move_input.hpp"
#include "movebeat/game.hpp"

namespace movebeat {
namespace ps3 {

// Deterministic emulator-only input for end-to-end render/game-loop checks.
// This never stands in for actual PlayStation Move tracking on hardware.
MoveFrame scriptedDemoFrame(const Game& game, unsigned int now_ms,
                            bool automatic_cuts = true);

} // namespace ps3
} // namespace movebeat

#endif
