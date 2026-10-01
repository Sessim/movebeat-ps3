#include "sound_effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace movebeat {
namespace ps3 {
namespace {

const float kTwoPi = 6.28318530717958647692f;

class Noise {
public:
    Noise() : state_(0x2545f491u) {}
    float next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(state_ & 0xffffu) / 32768.0f - 1.0f;
    }

private:
    std::uint32_t state_;
};

std::vector<float> make(unsigned int rate, float seconds) {
    return std::vector<float>(static_cast<std::size_t>(rate * seconds), 0.0f);
}

// Short attack so no effect starts with a click of its own.
float attack(float t, float length) {
    return t < length ? t / length : 1.0f;
}

void normalize(std::vector<float>& sound, float peak) {
    float maximum = 0.0f;
    for (std::size_t index = 0; index < sound.size(); ++index)
        maximum = std::max(maximum, std::fabs(sound[index]));
    if (maximum <= 0.0f) return;
    const float scale = peak / maximum;
    for (std::size_t index = 0; index < sound.size(); ++index)
        sound[index] *= scale;
}

} // namespace

void SoundBank::generate(unsigned int rate) {
    const float r = static_cast<float>(rate);
    Noise noise;

    // Tick: a bright, very short wooden click.
    std::vector<float>& tick = sounds_[SFX_TICK] = make(rate, 0.012f);
    for (std::size_t i = 0; i < tick.size(); ++i) {
        const float t = i / r;
        tick[i] = (std::sin(kTwoPi * 2600.0f * t) * 0.8f + noise.next() * 0.2f) *
                  std::exp(-t / 0.0015f) * attack(t, 0.0003f);
    }
    normalize(tick, 0.45f);

    // Difficulty: a rising two-tone "tock".
    std::vector<float>& tock = sounds_[SFX_DIFFICULTY] = make(rate, 0.06f);
    float phase = 0.0f;
    for (std::size_t i = 0; i < tock.size(); ++i) {
        const float t = i / r;
        const float frequency = 900.0f + 500.0f * std::min(1.0f, t / 0.025f);
        phase += kTwoPi * frequency / r;
        tock[i] = std::sin(phase) * std::exp(-t / 0.012f) * attack(t, 0.001f);
    }
    normalize(tock, 0.5f);

    // Land: a soft two-note chime (fifth) for the randomizer and level start.
    std::vector<float>& land = sounds_[SFX_LAND] = make(rate, 0.25f);
    for (std::size_t i = 0; i < land.size(); ++i) {
        const float t = i / r;
        land[i] = (std::sin(kTwoPi * 660.0f * t) +
                   0.7f * std::sin(kTwoPi * 990.0f * t)) *
                  std::exp(-t / 0.07f) * attack(t, 0.002f);
    }
    normalize(land, 0.5f);

    // Cut: a swept noise "slice" over a short low thump.
    std::vector<float>& cut = sounds_[SFX_CUT] = make(rate, 0.09f);
    float low = 0.0f;
    float previous = 0.0f;
    for (std::size_t i = 0; i < cut.size(); ++i) {
        const float t = i / r;
        // One-pole low-pass whose cutoff falls: bright hiss to darker swish.
        const float cutoff = 0.85f - 0.6f * std::min(1.0f, t / 0.09f);
        low += (noise.next() - low) * cutoff;
        const float hiss = low - previous * 0.6f;
        previous = low;
        const float thump = std::sin(kTwoPi * 160.0f * t) * std::exp(-t / 0.02f);
        cut[i] = (hiss * 0.8f + thump * 0.6f) * std::exp(-t / 0.025f) *
                 attack(t, 0.0015f);
    }
    normalize(cut, 0.55f);

    // Bomb: a dull, longer low rumble.
    std::vector<float>& bomb = sounds_[SFX_BOMB] = make(rate, 0.2f);
    low = 0.0f;
    for (std::size_t i = 0; i < bomb.size(); ++i) {
        const float t = i / r;
        low += (noise.next() - low) * 0.05f;
        bomb[i] = (low * 2.0f + std::sin(kTwoPi * 70.0f * t) * 0.6f) *
                  std::exp(-t / 0.06f) * attack(t, 0.003f);
    }
    normalize(bomb, 0.6f);

    // Miss: a short falling tone.
    std::vector<float>& miss = sounds_[SFX_MISS] = make(rate, 0.1f);
    phase = 0.0f;
    for (std::size_t i = 0; i < miss.size(); ++i) {
        const float t = i / r;
        phase += kTwoPi * (260.0f - 120.0f * t / 0.1f) / r;
        miss[i] = std::sin(phase) * std::exp(-t / 0.03f) * attack(t, 0.002f);
    }
    normalize(miss, 0.35f);
}

EffectMixer::EffectMixer() : bank_(0), write_(0), read_(0), played_(0) {
    for (int index = 0; index < kVoices; ++index) voices_[index].sound = 0;
}

bool EffectMixer::play(SoundEffect effect, float gain, float pan) {
    const unsigned int write = write_.load(std::memory_order_relaxed);
    if (write - read_.load(std::memory_order_acquire) >= kQueue) return false;
    Request& request = queue_[write % kQueue];
    request.effect = effect;
    request.gain = gain;
    request.pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
    write_.store(write + 1u, std::memory_order_release);
    return true;
}

void EffectMixer::mix(float* stereo, unsigned int frames) {
    if (!bank_) return;
    const unsigned int write = write_.load(std::memory_order_acquire);
    unsigned int read = read_.load(std::memory_order_relaxed);
    while (read != write) {
        const Request& request = queue_[read % kQueue];
        // Take a free voice, or steal the one closest to its end.
        int chosen = -1;
        std::size_t least_left = static_cast<std::size_t>(-1);
        for (int index = 0; index < kVoices; ++index) {
            const Voice& voice = voices_[index];
            if (!voice.sound) { chosen = index; break; }
            const std::size_t left = voice.sound->size() - voice.position;
            if (left < least_left) { least_left = left; chosen = index; }
        }
        Voice& voice = voices_[chosen];
        voice.sound = &bank_->sound(static_cast<SoundEffect>(request.effect));
        voice.position = 0;
        // Constant-power pan.
        const float angle = (request.pan + 1.0f) * 0.25f * 3.14159265f;
        voice.left = request.gain * std::cos(angle);
        voice.right = request.gain * std::sin(angle);
        played_.fetch_add(1u);
        ++read;
    }
    read_.store(read, std::memory_order_release);
    for (int index = 0; index < kVoices; ++index) {
        Voice& voice = voices_[index];
        if (!voice.sound) continue;
        const std::vector<float>& sound = *voice.sound;
        for (unsigned int frame = 0; frame < frames; ++frame) {
            if (voice.position >= sound.size()) break;
            const float sample = sound[voice.position++];
            stereo[frame * 2] += sample * voice.left;
            stereo[frame * 2 + 1] += sample * voice.right;
        }
        if (voice.position >= sound.size()) voice.sound = 0;
    }
    for (unsigned int index = 0; index < frames * 2u; ++index) {
        if (stereo[index] > 1.0f) stereo[index] = 1.0f;
        else if (stereo[index] < -1.0f) stereo[index] = -1.0f;
    }
}

} // namespace ps3
} // namespace movebeat
