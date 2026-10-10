#ifndef MOVEBEAT_PS3_AUDIO_CACHE_HPP
#define MOVEBEAT_PS3_AUDIO_CACHE_HPP

#include <stdio.h>

#include <string>

namespace movebeat {
namespace ps3 {

// A level's music read from a USB drive would share the bus with the PS
// Eye during play. This copies it to a file on the hard disk a piece per
// frame (while the player calibrates, or during the countdown before the
// next playlist song), so the song plays from the disk. One slot: a new
// source replaces the cached one.
class AudioCache {
public:
    AudioCache() : source_file_(0), target_file_(0), ready_(false), failed_(false),
                   copied_(0) {}
    ~AudioCache() { abandon(); }

    // Paths on USB drives go through the cache.
    static bool needsCache(const std::string& path) {
        return path.compare(0, 8, "/dev_usb") == 0;
    }

    // Starts copying `source` into `target` (nothing to do when it is the
    // source already cached or being copied).
    void begin(const std::string& source, const std::string& target) {
        if (source == source_ && target == target_ && (ready_ || source_file_)) return;
        abandon();
        source_ = source;
        target_ = target;
        ready_ = failed_ = false;
        copied_ = 0;
        source_file_ = fopen(source.c_str(), "rb");
        target_file_ = source_file_ ? fopen(target.c_str(), "wb") : 0;
        if (!source_file_ || !target_file_) fail();
    }

    // Copies up to `max_bytes`; true once the copy is complete.
    bool step(unsigned int max_bytes) {
        if (ready_) return true;
        if (!source_file_) return false;
        char buffer[32 * 1024];
        unsigned int moved = 0;
        while (moved < max_bytes) {
            const unsigned int want = max_bytes - moved < sizeof(buffer)
                ? max_bytes - moved : static_cast<unsigned int>(sizeof(buffer));
            const size_t count = fread(buffer, 1, want, source_file_);
            if (count > 0 && fwrite(buffer, 1, count, target_file_) != count) {
                fail();
                return false;
            }
            moved += static_cast<unsigned int>(count);
            copied_ += static_cast<unsigned long>(count);
            if (count < want) {
                if (ferror(source_file_)) {
                    fail();
                    return false;
                }
                const bool closed_ok = fclose(target_file_) == 0;
                target_file_ = 0;
                fclose(source_file_);
                source_file_ = 0;
                if (!closed_ok) {
                    fail();
                    return false;
                }
                ready_ = true;
                return true;
            }
        }
        return false;
    }

    // Finishes the copy now; true when it is complete.
    bool finish() {
        while (source_file_ && !step(1024u * 1024u)) {}
        return ready_;
    }

    // The file to play for `source`: the disk copy when it is complete,
    // otherwise the source itself.
    std::string playable(const std::string& source) const {
        return ready_ && source == source_ ? target_ : source;
    }

    bool busy() const { return source_file_ != 0; }
    bool ready() const { return ready_; }
    bool failed() const { return failed_; }
    unsigned long copiedBytes() const { return copied_; }
    const std::string& source() const { return source_; }

private:
    void abandon() {
        if (source_file_) fclose(source_file_);
        if (target_file_) fclose(target_file_);
        source_file_ = target_file_ = 0;
        ready_ = false;
    }
    void fail() {
        if (target_file_) {
            fclose(target_file_);
            target_file_ = 0;
            remove(target_.c_str());
        }
        if (source_file_) fclose(source_file_);
        source_file_ = 0;
        failed_ = true;
        ready_ = false;
    }

    std::string source_;
    std::string target_;
    FILE* source_file_;
    FILE* target_file_;
    bool ready_;
    bool failed_;
    unsigned long copied_;

    AudioCache(const AudioCache&);
    AudioCache& operator=(const AudioCache&);
};

} // namespace ps3
} // namespace movebeat

#endif
