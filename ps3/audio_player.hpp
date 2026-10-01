#ifndef MOVEBEAT_PS3_AUDIO_PLAYER_HPP
#define MOVEBEAT_PS3_AUDIO_PLAYER_HPP

#include <string>

#include "sound_effects.hpp"

namespace movebeat {
namespace ps3 {

// 48 kHz stereo output that stays open while the app runs. One Ogg/Vorbis
// music stream (a level's song or a library preview) and short synthesized
// effects are mixed into it on the audio thread. The gameplay clock counts
// only music samples, and only while the music is not paused.
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    // Starts a level's song from the beginning at full volume and resets the
    // clock. Opens the output first if needed.
    bool start(const std::string& path);
    // Starts a library preview at `offset_fraction` of the song (capped at
    // `max_offset_ms`), fading in over `fade_ms`.
    bool startPreview(const std::string& path, float offset_fraction,
                      unsigned int max_offset_ms, unsigned int fade_ms);
    // Fades the music out and stops it; returns immediately.
    void fadeOut(unsigned int fade_ms);
    // Pauses/resumes the music and its clock; effects keep playing.
    bool pause();
    bool resume();
    // Stops the music now (waits at most one audio block). Output stays open.
    void shutdown();
    // Closes the output entirely (application exit).
    void close();

    // Queues a sound effect; opens the output if needed.
    void playEffect(SoundEffect effect, float gain, float pan);

    unsigned int timeMs() const;
    unsigned int durationMs() const;
    bool finished() const;
    // True while a music stream is playing or fading.
    bool musicActive() const;
    unsigned int effectsPlayed() const;
    int lastError() const;

private:
    struct Impl;
    Impl* impl_;

    bool openOutput();
    bool openMusic(const std::string& path);

    AudioPlayer(const AudioPlayer&);
    AudioPlayer& operator=(const AudioPlayer&);
};

} // namespace ps3
} // namespace movebeat

#endif
