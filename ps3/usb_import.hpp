#ifndef MOVEBEAT_PS3_USB_IMPORT_HPP
#define MOVEBEAT_PS3_USB_IMPORT_HPP

#include <stdio.h>

#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#elif defined(__PPU__)
#include <sys/file.h>
#else
#include <sys/stat.h>
#endif

namespace movebeat {
namespace ps3 {

inline void makeDirectory(const std::string& path) {
#if defined(_WIN32)
    _mkdir(path.c_str());
#elif defined(__PPU__)
    sysLv2FsMkdir(path.c_str(), 0755);
#else
    mkdir(path.c_str(), 0755);
#endif
}

// One bundle folder to copy from a USB drive into the hard-disk library.
struct UsbImportJob {
    std::string source;
    std::string target;
    // File names in the bundle, the manifest (level.ini / playlist.ini)
    // last: a folder interrupted mid-copy has no manifest, so the library
    // never lists it and the next import copies it again.
    std::vector<std::string> files;
};

// Copies queued bundles a piece per frame (in the menus only: the PS Eye
// shares the USB bus during play). Each file goes to "<name>.part" and is
// renamed when complete.
class UsbImport {
public:
    UsbImport() : next_(0), file_(0), source_file_(0), target_file_(0),
                  done_(0), failed_(0) {}
    ~UsbImport() { closeFiles(); }

    void add(const UsbImportJob& job) {
        for (std::size_t index = next_; index < jobs_.size(); ++index)
            if (jobs_[index].source == job.source) return;
        jobs_.push_back(job);
    }

    bool busy() const { return next_ < jobs_.size(); }
    // Bundles finished, failed and queued since the last idle moment.
    unsigned int done() const { return done_; }
    unsigned int failed() const { return failed_; }
    unsigned int total() const { return static_cast<unsigned int>(jobs_.size()); }
    // Forgets finished jobs (counters too) once nothing is queued.
    void clearFinished() {
        if (busy()) return;
        jobs_.clear();
        next_ = 0;
        done_ = failed_ = 0;
    }

    // Copies up to `max_bytes`. When a bundle is complete, returns true
    // with its source and target folders.
    bool step(unsigned int max_bytes, std::string& source, std::string& target) {
        unsigned int moved = 0;
        while (busy() && moved < max_bytes) {
            const UsbImportJob& job = jobs_[next_];
            if (!source_file_) {
                if (file_ == 0) makeDirectory(job.target);
                if (file_ >= job.files.size()) {
                    source = job.source;
                    target = job.target;
                    ++done_;
                    nextJob();
                    return true;
                }
                const std::string from = job.source + "/" + job.files[file_];
                const std::string part = job.target + "/" + job.files[file_] + ".part";
                source_file_ = fopen(from.c_str(), "rb");
                target_file_ = source_file_ ? fopen(part.c_str(), "wb") : 0;
                if (!source_file_ || !target_file_) {
                    failJob();
                    continue;
                }
            }
            char buffer[32 * 1024];
            const unsigned int want = max_bytes - moved < sizeof(buffer)
                ? max_bytes - moved : static_cast<unsigned int>(sizeof(buffer));
            const size_t count = fread(buffer, 1, want, source_file_);
            if (count > 0 && fwrite(buffer, 1, count, target_file_) != count) {
                failJob();
                continue;
            }
            moved += static_cast<unsigned int>(count);
            if (count < want) {
                const bool read_ok = !ferror(source_file_);
                const bool closed_ok = fclose(target_file_) == 0;
                target_file_ = 0;
                fclose(source_file_);
                source_file_ = 0;
                const std::string part = job.target + "/" + job.files[file_] + ".part";
                const std::string final_path = job.target + "/" + job.files[file_];
                remove(final_path.c_str());
                if (!read_ok || !closed_ok || rename(part.c_str(), final_path.c_str()) != 0) {
                    remove(part.c_str());
                    failJob();
                    continue;
                }
                ++file_;
            }
        }
        return false;
    }

private:
    void closeFiles() {
        if (source_file_) fclose(source_file_);
        if (target_file_) fclose(target_file_);
        source_file_ = target_file_ = 0;
    }
    void nextJob() {
        closeFiles();
        ++next_;
        file_ = 0;
    }
    void failJob() {
        if (target_file_) {
            fclose(target_file_);
            target_file_ = 0;
            const UsbImportJob& job = jobs_[next_];
            if (file_ < job.files.size())
                remove((job.target + "/" + job.files[file_] + ".part").c_str());
        }
        ++failed_;
        nextJob();
    }

    std::vector<UsbImportJob> jobs_;
    std::size_t next_;
    std::size_t file_;
    FILE* source_file_;
    FILE* target_file_;
    unsigned int done_;
    unsigned int failed_;

    UsbImport(const UsbImport&);
    UsbImport& operator=(const UsbImport&);
};

} // namespace ps3
} // namespace movebeat

#endif
