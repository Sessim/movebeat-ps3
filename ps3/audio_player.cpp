#include "audio_player.hpp"

#include <atomic>
#include <stddef.h>
#include <string.h>

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

struct AudioPlayer::Impl {
    bool vorbis_open;
    bool audio_initialized;
    bool port_open;
    bool queue_created;
    bool queue_attached;
    bool port_started;
    std::atomic<bool> running;
    std::atomic<bool> paused;
    std::atomic<bool> stream_finished;
    std::atomic<u64> played_samples;
    unsigned int duration_ms;
    std::atomic<int> decode_error;
    int last_error;
    u32 port;
    sys_event_queue_t queue;
    sys_ipc_key_t queue_key;
    sys_ppu_thread_t thread;
    audioPortConfig config;
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

    Impl()
        : vorbis_open(false), audio_initialized(false), port_open(false),
          queue_created(false), queue_attached(false), port_started(false),
          running(false), paused(false), stream_finished(false), played_samples(0),
          duration_ms(0),
          decode_error(0),
          last_error(0), port(0), queue(0), queue_key(0), thread(0),
          input_rate(0), input_channels(0), decoded_frames(0),
          decoded_position(0), phase(0.0), have_source(false) {
        memset(&config, 0, sizeof(config));
        memset(&vorbis, 0, sizeof(vorbis));
        current[0] = current[1] = next[0] = next[1] = 0.0f;
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

    void fill(float* output) {
        if (!have_source) {
            have_source = readSourceFrame(current) && readSourceFrame(next);
            if (!have_source) stream_finished.store(true);
        }
        const double step = static_cast<double>(input_rate) / kOutputRate;
        for (unsigned int sample = 0; sample < AUDIO_BLOCK_SAMPLES; ++sample) {
            if (!have_source) {
                output[sample * 2] = output[sample * 2 + 1] = 0.0f;
                continue;
            }
            output[sample * 2] = current[0] +
                static_cast<float>((next[0] - current[0]) * phase);
            output[sample * 2 + 1] = current[1] +
                static_cast<float>((next[1] - current[1]) * phase);
            phase += step;
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
    }

    void fillNextBlock() {
        const volatile u64* read_index = reinterpret_cast<volatile u64*>(
            static_cast<u64>(config.readIndex));
        const u64 next_block = (*read_index + 1) % config.numBlocks;
        float* data = reinterpret_cast<float*>(static_cast<u64>(config.audioDataStart));
        fill(data + next_block * config.channelCount * AUDIO_BLOCK_SAMPLES);
    }

    static void threadEntry(void* argument) {
        Impl* self = static_cast<Impl*>(argument);
        sys_event_t event;
        while (self->running.load()) {
            if (sysEventQueueReceive(self->queue, &event, 20 * 1000) == 0) {
                if (self->paused.load()) continue;
                self->played_samples.fetch_add(AUDIO_BLOCK_SAMPLES);
                self->fillNextBlock();
            }
        }
        sysThreadExit(0);
    }
};

AudioPlayer::AudioPlayer() : impl_(new Impl()) {}

AudioPlayer::~AudioPlayer() {
    shutdown();
    delete impl_;
}

bool AudioPlayer::start(const std::string& path) {
    shutdown();
    impl_->last_error = ov_fopen(path.c_str(), &impl_->vorbis);
    if (impl_->last_error != 0) return false;
    impl_->vorbis_open = true;
    vorbis_info* info = ov_info(&impl_->vorbis, -1);
    if (!info || info->rate <= 0 || (info->channels != 1 && info->channels != 2)) {
        impl_->last_error = -2001;
        shutdown();
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

    impl_->last_error = audioInit();
    if (impl_->last_error != 0) { shutdown(); return false; }
    impl_->audio_initialized = true;
    audioPortParam parameters;
    parameters.numChannels = AUDIO_PORT_2CH;
    parameters.numBlocks = AUDIO_BLOCK_8;
    parameters.attrib = AUDIO_PORT_INITLEVEL;
    parameters.level = 1.0f;
    impl_->last_error = audioPortOpen(&parameters, &impl_->port);
    if (impl_->last_error != 0) { shutdown(); return false; }
    impl_->port_open = true;
    if ((impl_->last_error = audioGetPortConfig(impl_->port, &impl_->config)) != 0 ||
        (impl_->last_error = audioCreateNotifyEventQueue(
            &impl_->queue, &impl_->queue_key)) != 0) {
        shutdown();
        return false;
    }
    impl_->queue_created = true;
    if ((impl_->last_error = audioSetNotifyEventQueue(impl_->queue_key)) != 0) {
        shutdown();
        return false;
    }
    impl_->queue_attached = true;
    sysEventQueueDrain(impl_->queue);
    impl_->fillNextBlock();
    if (impl_->decode_error.load()) {
        impl_->last_error = impl_->decode_error.load();
        shutdown();
        return false;
    }
    if ((impl_->last_error = audioPortStart(impl_->port)) != 0) {
        shutdown();
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
        shutdown();
        return false;
    }
    impl_->last_error = 0;
    return true;
}

bool AudioPlayer::pause() {
    if (!impl_ || !impl_->port_started) return true;
    // Publish the pause before stopping the port so an already-woken worker
    // cannot advance the gameplay clock from a stale notification.
    impl_->paused.store(true);
    impl_->last_error = audioPortStop(impl_->port);
    if (impl_->last_error != 0) {
        impl_->paused.store(false);
        return false;
    }
    impl_->port_started = false;
    sysEventQueueDrain(impl_->queue);
    return true;
}

bool AudioPlayer::resume() {
    if (!impl_ || !impl_->port_open || impl_->port_started) return true;
    sysEventQueueDrain(impl_->queue);
    impl_->paused.store(false);
    impl_->last_error = audioPortStart(impl_->port);
    if (impl_->last_error != 0) {
        impl_->paused.store(true);
        return false;
    }
    impl_->port_started = true;
    return true;
}

void AudioPlayer::shutdown() {
    if (!impl_) return;
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
    if (impl_->vorbis_open) { ov_clear(&impl_->vorbis); impl_->vorbis_open = false; }
    impl_->played_samples.store(0);
    impl_->duration_ms = 0;
    impl_->paused.store(false);
    impl_->stream_finished.store(false);
    impl_->decode_error.store(0);
    impl_->decoded_frames = impl_->decoded_position = 0;
    impl_->phase = 0.0;
    impl_->have_source = false;
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

int AudioPlayer::lastError() const {
    if (!impl_) return -1;
    const int decoder = impl_->decode_error.load();
    return decoder ? decoder : impl_->last_error;
}

} // namespace ps3
} // namespace movebeat
