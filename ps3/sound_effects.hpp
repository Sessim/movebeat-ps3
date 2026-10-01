#ifndef MOVEBEAT_PS3_SOUND_EFFECTS_HPP
#define MOVEBEAT_PS3_SOUND_EFFECTS_HPP

#include <atomic>
#include <cstddef>
#include <vector>

namespace movebeat {
namespace ps3 {

enum SoundEffect {
    SFX_TICK,        // carousel passes a row
    SFX_DIFFICULTY,  // difficulty changes
    SFX_LAND,        // randomizer lands, level starts
    SFX_CUT,         // a note is cut
    SFX_BOMB,        // a bomb is hit
    SFX_MISS,        // a note is missed
    SFX_COUNT
};

// Short mono effects synthesized at startup (no sampled assets), at the
// output rate. Each is a few milliseconds to a fifth of a second.
class SoundBank {
public:
    void generate(unsigned int rate);
    const std::vector<float>& sound(SoundEffect effect) const {
        return sounds_[effect];
    }

private:
    std::vector<float> sounds_[SFX_COUNT];
};

// Mixes up to kVoices effects into stereo float output. play() is called
// from the game thread and mix() from the audio thread: requests go through
// a single-producer/single-consumer ring, so neither side ever blocks.
class EffectMixer {
public:
    enum { kVoices = 8, kQueue = 32 };

    EffectMixer();

    // The bank must outlive the mixer and must not change after this.
    void attach(const SoundBank* bank) { bank_ = bank; }
    // pan: -1 left ... +1 right. Returns false if the queue is full.
    bool play(SoundEffect effect, float gain, float pan);
    // Adds the active voices to `frames` interleaved stereo samples.
    void mix(float* stereo, unsigned int frames);
    unsigned int playedCount() const { return played_.load(); }

private:
    struct Request {
        int effect;
        float gain;
        float pan;
    };
    struct Voice {
        const std::vector<float>* sound;
        std::size_t position;
        float left;
        float right;
    };

    const SoundBank* bank_;
    Request queue_[kQueue];
    std::atomic<unsigned int> write_;
    std::atomic<unsigned int> read_;
    Voice voices_[kVoices];
    std::atomic<unsigned int> played_;
};

} // namespace ps3
} // namespace movebeat

#endif
