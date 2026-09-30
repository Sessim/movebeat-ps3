#ifndef MOVEBEAT_PS3_AUDIO_PLAYER_HPP
#define MOVEBEAT_PS3_AUDIO_PLAYER_HPP

#include <string>

namespace movebeat {
namespace ps3 {

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool start(const std::string& path);
    bool pause();
    bool resume();
    void shutdown();
    unsigned int timeMs() const;
    unsigned int durationMs() const;
    bool finished() const;
    int lastError() const;

private:
    struct Impl;
    Impl* impl_;

    AudioPlayer(const AudioPlayer&);
    AudioPlayer& operator=(const AudioPlayer&);
};

} // namespace ps3
} // namespace movebeat

#endif
