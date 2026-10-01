#include "audio_player.hpp"

#include <atomic>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include <audio/audio.h>
#include <sys/event_queue.h>
#include <sys/thread.h>
#include <vorbis/vorbisfile.h>

namespace movebeat {
namespace ps3 {
namespace {

const unsigned int kOutputRate = 48000;
const int kDecodeError = -2002;

} // namespace

// Threading: the game thread opens and closes music only while
// `music_enabled` is false and the audio thread is not inside a music block
// (`in_music` false). Every flag is a sequentially consistent atomic, so
// "game clears enabled, then waits for !in_music" and "audio sets in_music,
// then re-checks enabled" can never both proceed. Fade parameters are
// atomics the audio thread reads once per block.
struct AudioPlayer::Impl {
    // Output.
    bool audio_initialized;
    bool port_open;
    bool queue_created;
    bool queue_attached;
    bool port_started;
    bool output_failed;
    std::atomic<bool> running;
    u32 port;
    sys_event_queue_t queue;
    sys_ipc_key_t queue_key;
    sys_ppu_thread_t thread;
    audioPortConfig config;
    SoundBank bank;
    EffectMixer effects;

    // Music.
    std::atomic<bool> music_enabled;
    std::atomic<bool> in_music;
    std::atomic<bool> paused;
    std::atomic<bool> stream_finished;
    std::atomic<u64> played_samples;
    std::atomic<int> decode_error;
    std::atomic<float> fade_target;
    std::atomic<float> fade_step;
    std::atomic<bool> stop_when_silent;
    bool vorbis_open;
    unsigned int duration_ms;
    int last_error;
    OggVorbis_File vorbis;
    long input_rate;
    int input_channels;
    s16 decode_buffer[4096 * 2];
    long decoded_frames;
    long decoded_position;
    float current[2];
    float next[2];
    double phase;
    bool have_source;
    float gain;  // audio thread only while music is enabled

    Impl()
        : audio_initialized(false), port_open(false), queue_created(false),
          queue_attached(false), port_started(false), output_failed(false),
          running(false), port(0), queue(0), queue_key(0), thread(0),
          music_enabled(false), in_music(false), paused(false),
          stream_finished(false), played_samples(0), decode_error(0),
          fade_target(1.0f), fade_step(1.0f), stop_when_silent(false),
          vorbis_open(false), duration_ms(0), last_error(0),
          input_rate(0), input_channels(0), decoded_frames(0),
          decoded_position(0), phase(0.0), have_source(false), gain(1.0f) {
        memset(&config, 0, sizeof(config));
        memset(&vorbis, 0, sizeof(vorbis));
        current[0] = current[1] = next[0] = next[1] = 0.0f;
        bank.generate(kOutputRate);
        effects.attach(&bank);
    }

    bool readSourceFrame(float frame[2]) {
        unsigned int consecutive_errors = 0;
        while (decoded_position >= decoded_frames) {
            int bitstream = 0;
            const long bytes = ov_read(&vorbis,
                reinterpret_cast<char*>(decode_buffer), sizeof(decode_buffer),
                1, 2, 1, &bitstream);
            if (bytes == 0) return false;
            if (bytes < 0) {
                if (++consecutive_errors >= 8u) {
                    decode_error.store(kDecodeError);
                    return false;
                }
                continue;
            }
            consecutive_errors = 0;
            decoded_frames = bytes / (static_cast<long>(sizeof(s16)) * input_channels);
            decoded_position = 0;
        }
        const s16* sample = decode_buffer + decoded_position * input_channels;
        frame[0] = static_cast<float>(sample[0]) / 32768.0f;
        frame[1] = input_channels == 1
            ? frame[0] : static_cast<float>(sample[1]) / 32768.0f;
        ++decoded_position;
        return true;
    }

    // Adds one block of resampled music to `output` (audio thread).
    void mixMusic(float* output) {
        if (!have_source && !stream_finished.load()) {
            have_source = readSourceFrame(current) && readSourceFrame(next);
            if (!have_source) stream_finished.store(true);
        }
        const float target = fade_target.load();
        const float step = fade_step.load();
        const double ratio = static_cast<double>(input_rate) / kOutputRate;
        for (unsigned int sample = 0; sample < AUDIO_BLOCK_SAMPLES; ++sample) {
            if (gain < target) gain = gain + step < target ? gain + step : target;
            else if (gain > target) gain = gain - step > target ? gain - step : target;
            if (!have_source) continue;
            output[sample * 2] += gain * (current[0] +
                static_cast<float>((next[0] - current[0]) * phase));
            output[sample * 2 + 1] += gain * (current[1] +
                static_cast<float>((next[1] - current[1]) * phase));
            phase += ratio;
            while (phase >= 1.0 && have_source) {
                phase -= 1.0;
                current[0] = next[0];
                current[1] = next[1];
                if (!readSourceFrame(next)) {
                    have_source = false;
                    stream_finished.store(true);
                }
            }
        }
        if (stop_when_silent.load() && gain <= 0.0f) music_enabled.store(false);
    }

    void fillNextBlock() {
        const volatile u64* read_index = reinterpret_cast<volatile u64*>(
            static_cast<u64>(config.readIndex));
        const u64 next_block = (*read_index + 1) % config.numBlocks;
        float* data = reinterpret_cast<float*>(static_cast<u64>(config.audioDataStart)) +
            next_block * config.channelCount * AUDIO_BLOCK_SAMPLES;
        memset(data, 0, sizeof(float) * 2 * AUDIO_BLOCK_SAMPLES);
        if (music_enabled.load()) {
            in_music.store(true);
            if (music_enabled.load() && !paused.load()) {
                played_samples.fetch_add(AUDIO_BLOCK_SAMPLES);
                mixMusic(data);
            }
            in_music.store(false);
        }
        effects.mix(data, AUDIO_BLOCK_SAMPLES);
    }

    // Game thread: makes the music stream safe to close or replace.
    void stopMusic() {
        music_enabled.store(false);
        while (in_music.load()) usleep(200);
        if (vorbis_open) {
            ov_clear(&vorbis);
            vorbis_open = false;
        }
        paused.store(false);
        stream_finished.store(false);
        decode_error.store(0);
        stop_when_silent.store(false);
        duration_ms = 0;
        decoded_frames = decoded_position = 0;
        phase = 0.0;
        have_source = false;
    }

    static void threadEntry(void* argument) {
        Impl* self = static_cast<Impl*>(argument);
        sys_event_t event;
        while (self->running.load()) {
            if (sysEventQueueReceive(self->queue, &event, 20 * 1000) == 0)
                self->fillNextBlock();
        }
        sysThreadExit(0);
    }
};

AudioPlayer::AudioPlayer() : impl_(new Impl()) {}

AudioPlayer::~AudioPlayer() {
    close();
    delete impl_;
}

bool AudioPlayer::openOutput() {
    if (impl_->running.load()) return true;
    if (impl_->output_failed) return false;
    impl_->last_error = audioInit();
    if (impl_->last_error != 0) { close(); impl_->output_failed = true; return false; }
    impl_->audio_initialized = true;
    audioPortParam parameters;
    parameters.numChannels = AUDIO_PORT_2CH;
    parameters.numBlocks = AUDIO_BLOCK_8;
    parameters.attrib = AUDIO_PORT_INITLEVEL;
    parameters.level = 1.0f;
    impl_->last_error = audioPortOpen(&parameters, &impl_->port);
    if (impl_->last_error != 0) { close(); impl_->output_failed = true; return false; }
    impl_->port_open = true;
    if ((impl_->last_error = audioGetPortConfig(impl_->port, &impl_->config)) != 0 ||
        (impl_->last_error = audioCreateNotifyEventQueue(
            &impl_->queue, &impl_->queue_key)) != 0) {
        close();
        impl_->output_failed = true;
        return false;
    }
    impl_->queue_created = true;
    if ((impl_->last_error = audioSetNotifyEventQueue(impl_->queue_key)) != 0) {
        close();
        impl_->output_failed = true;
        return false;
    }
    impl_->queue_attached = true;
    sysEventQueueDrain(impl_->queue);
    // Silence every block before the port starts reading them.
    memset(reinterpret_cast<void*>(static_cast<u64>(impl_->config.audioDataStart)), 0,
           sizeof(float) * impl_->config.channelCount * AUDIO_BLOCK_SAMPLES *
           impl_->config.numBlocks);
    if ((impl_->last_error = audioPortStart(impl_->port)) != 0) {
        close();
        impl_->output_failed = true;
        return false;
    }
    impl_->port_started = true;
    impl_->running.store(true);
    static char thread_name[] = "movebeat audio";
    impl_->last_error = sysThreadCreate(&impl_->thread, Impl::threadEntry, impl_,
                                         1000, 64 * 1024, THREAD_JOINABLE,
                                         thread_name);
    if (impl_->last_error != 0) {
        impl_->running.store(false);
        close();
        impl_->output_failed = true;
        return false;
    }
    impl_->last_error = 0;
    return true;
}

bool AudioPlayer::openMusic(const std::string& path) {
    impl_->stopMusic();
    impl_->last_error = ov_fopen(path.c_str(), &impl_->vorbis);
    if (impl_->last_error != 0) return false;
    impl_->vorbis_open = true;
    vorbis_info* info = ov_info(&impl_->vorbis, -1);
    if (!info || info->rate <= 0 || (info->channels != 1 && info->channels != 2)) {
        impl_->last_error = -2001;
        impl_->stopMusic();
        return false;
    }
    impl_->input_rate = info->rate;
    impl_->input_channels = info->channels;
    const double duration_seconds = ov_time_total(&impl_->vorbis, -1);
    if (duration_seconds > 0.0) {
        const double duration_ms = duration_seconds * 1000.0 + 0.5;
        impl_->duration_ms = duration_ms >= 4294967295.0
            ? 0xffffffffu : static_cast<unsigned int>(duration_ms);
    }
    return true;
}

bool AudioPlayer::start(const std::string& path) {
    if (!openMusic(path)) return false;
    if (!openOutput()) {
        impl_->stopMusic();
        return false;
    }
    impl_->played_samples.store(0);
    impl_->gain = 1.0f;
    impl_->fade_target.store(1.0f);
    impl_->fade_step.store(1.0f);
    impl_->music_enabled.store(true);
    impl_->last_error = 0;
    return true;
}

bool AudioPlayer::startPreview(const std::string& path, float offset_fraction,
                               unsigned int max_offset_ms, unsigned int fade_ms) {
    if (!openMusic(path)) return false;
    if (!openOutput()) {
        impl_->stopMusic();
        return false;
    }
    double offset_ms = impl_->duration_ms * static_cast<double>(offset_fraction);
    if (offset_ms > max_offset_ms) offset_ms = max_offset_ms;
    if (offset_ms > 0.0) ov_time_seek(&impl_->vorbis, offset_ms / 1000.0);
    impl_->played_samples.store(0);
    impl_->gain = 0.0f;
    impl_->fade_target.store(1.0f);
    impl_->fade_step.store(fade_ms ? 1.0f / (fade_ms * (kOutputRate / 1000.0f)) : 1.0f);
    impl_->music_enabled.store(true);
    impl_->last_error = 0;
    return true;
}

void AudioPlayer::fadeOut(unsigned int fade_ms) {
    if (!impl_->music_enabled.load()) return;
    impl_->fade_step.store(fade_ms ? 1.0f / (fade_ms * (kOutputRate / 1000.0f)) : 1.0f);
    impl_->stop_when_silent.store(true);
    impl_->fade_target.store(0.0f);
}

bool AudioPlayer::pause() {
    impl_->paused.store(true);
    return true;
}

bool AudioPlayer::resume() {
    impl_->paused.store(false);
    return true;
}

void AudioPlayer::shutdown() {
    impl_->stopMusic();
    impl_->played_samples.store(0);
}

void AudioPlayer::close() {
    if (!impl_) return;
    impl_->stopMusic();
    if (impl_->running.load()) {
        impl_->running.store(false);
        u64 result = 0;
        sysThreadJoin(impl_->thread, &result);
    }
    if (impl_->port_started) { audioPortStop(impl_->port); impl_->port_started = false; }
    if (impl_->queue_attached) {
        audioRemoveNotifyEventQueue(impl_->queue_key);
        impl_->queue_attached = false;
    }
    if (impl_->port_open) { audioPortClose(impl_->port); impl_->port_open = false; }
    if (impl_->queue_created) {
        sysEventQueueDestroy(impl_->queue, 0);
        impl_->queue_created = false;
    }
    if (impl_->audio_initialized) { audioQuit(); impl_->audio_initialized = false; }
    impl_->played_samples.store(0);
}

void AudioPlayer::playEffect(SoundEffect effect, float gain, float pan) {
    if (!openOutput()) return;
    impl_->effects.play(effect, gain, pan);
}

unsigned int AudioPlayer::timeMs() const {
    return impl_ ? static_cast<unsigned int>(
        impl_->played_samples.load() * 1000 / kOutputRate) : 0;
}

unsigned int AudioPlayer::durationMs() const {
    return impl_ ? impl_->duration_ms : 0;
}

bool AudioPlayer::finished() const {
    return impl_ && impl_->stream_finished.load();
}

bool AudioPlayer::musicActive() const {
    return impl_ && impl_->music_enabled.load();
}

unsigned int AudioPlayer::effectsPlayed() const {
    return impl_ ? impl_->effects.playedCount() : 0;
}

int AudioPlayer::lastError() const {
    if (!impl_) return -1;
    const int decoder = impl_->decode_error.load();
    return decoder ? decoder : impl_->last_error;
}

} // namespace ps3
} // namespace movebeat
