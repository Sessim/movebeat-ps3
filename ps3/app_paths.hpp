#pragma once

namespace movebeat {
namespace ps3 {

// Files the PS3 application reads or writes inside its installed USRDIR.
const char kLevelRoot[] = "/dev_hdd0/game/MOVEBEAT1/USRDIR/levels";
// The hard-disk copy of a USB level's music (one slot, see AudioCache).
const char kUsbAudioCachePath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/usb_audio_cache.ogg";
// Level bundles on USB drives 0..7 (printf format with the drive number).
const char kUsbLevelRootFormat[] = "/dev_usb%03d/MOVEBEAT/levels";
const char kScoreLog[] = "/dev_hdd0/game/MOVEBEAT1/USRDIR/scores.log";
const char kSettingsPath[] = "/dev_hdd0/game/MOVEBEAT1/USRDIR/settings.ini";
const char kCapturePath[] = "/dev_hdd0/game/MOVEBEAT1/USRDIR/gem_capture.csv";
// Every time the camera lost a Move during the latest song.
const char kTrackingLogPath[] = "/dev_hdd0/game/MOVEBEAT1/USRDIR/tracking_log.csv";
const char kStartupReportPath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/startup_error.txt";
const char kRunStatusPath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/run_status.ini";
const char kFirstFramePath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/first_frame.ppm";
const char kRendererPath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/renderer.ini";
const char kDemoModePath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/demo_mode.ini";
const char kDemoFramePath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/demo_frame.ppm";
const char kDemoTimedFramePath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/demo_timed_frame.ppm";
const char kDemoScoredFramePath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/demo_scored_frame.ppm";

} // namespace ps3
} // namespace movebeat
