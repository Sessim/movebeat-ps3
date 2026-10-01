#ifndef MOVEBEAT_PS3_STUB_API_HPP
#define MOVEBEAT_PS3_STUB_API_HPP

#include <cstddef>
#include <cstdint>

typedef std::uint8_t u8;
typedef std::uint16_t u16;
typedef std::uint32_t u32;
typedef std::uint64_t u64;
typedef std::int16_t s16;
typedef std::int32_t s32;
typedef std::int64_t s64;
typedef float f32;
typedef s64 system_time_t;
typedef u64 sys_ipc_key_t;
typedef u64 sys_event_queue_t;
typedef u64 sys_ppu_thread_t;
typedef u32 sys_mem_container_t;

struct vec_float4 { float value[4]; };
struct sys_event_t { u64 source, data1, data2, data3; };
struct gcmContextData {};

struct gemPadData { u16 buttons; u16 ANA_T; };
struct gemInertialState { gemPadData pad; };
struct gemState {
    vec_float4 pos, vel, accel, quat, angvel, angaccel;
    vec_float4 handle_pos, handle_vel, handle_accel;
    gemPadData paddata;
    unsigned char reserved[64];
    system_time_t time;
    f32 temperature, camera_pitch_angle;
    u32 tracking;
};
struct gemImageState {
    system_time_t frame_time, time;
    f32 u, v, r, projectionx, projectiony, distance;
    u8 visible, r_valid;
};
struct gemInfo { u32 max, connected, status[4], port[4]; };
struct Spurs {};
struct SpursAttribute {};
struct gemAttribute {
    u32 version, max;
    void* memory;
    Spurs* spurs;
    u8 spu_priorities[8];
};

enum cameraType { CAM_TYPE_UNKNOWN, CAM_TYPE_EYETOY, CAM_TYPE_PLAYSTATION_EYE };
enum cameraFormat { CAM_FORM_UNKNOWN, CAM_FORM_JPG, CAM_FORM_RAW8 };
enum cameraResolution { CAM_RESO_UNKNOWN, CAM_RESO_VGA };
struct cameraInfoEx {
    cameraFormat format;
    cameraResolution resolution;
    s32 framerate;
    u32 buffer;
    s32 bytesize, width, height, dev_num, guid, info_ver;
    sys_mem_container_t container;
    s32 readmode;
    u32 pbuf[2];
};
struct cameraReadInfo { s32 version; u32 frame, readcount; s64 timestamp; u32 buffer; };

struct audioPortParam { u64 numChannels, numBlocks, attrib; f32 level; };
struct audioPortConfig {
    u32 readIndex, status;
    u64 channelCount, numBlocks;
    u32 portSize, audioDataStart;
};

struct videoDisplayMode {
    u8 resolution, scanMode, conversion, aspect, padding[2];
    u16 refreshRates;
};
struct videoState { u8 state, colorSpace, padding[6]; videoDisplayMode displayMode; };
struct videoResolution { u16 width, height; };
struct videoConfiguration { u8 resolution, format, aspect, padding[9]; u32 pitch; };

struct OggVorbis_File { unsigned char opaque[1024]; };
struct vorbis_info { int version, channels; long rate; };

#define MOVE_VERSION 2
#define GEM_TRACKING_POSITION_TRACKED 1
#define GEM_TRACKING_VISIBLE 2
#define STATE_CURRENT_TIME 0
#define GEM_INERTIAL_LATEST 0
#define SPURS_ALIGN 128
#define SYSMODULE_OK 0
#define SYSMODULE_ERR_DUPLICATE 1
#define SYSMODULE_CAMERA 1
#define SYSMODULE_GEM 2
#define THREAD_JOINABLE 1
#define AUDIO_BLOCK_SAMPLES 256
#define AUDIO_PORT_2CH 2
#define AUDIO_BLOCK_8 8
#define AUDIO_PORT_INITLEVEL 0x1000
#define VIDEO_BUFFER_FORMAT_XRGB 0
#define VIDEO_PRIMARY 0
#define VIDEO_ASPECT_AUTO 0
#define VIDEO_RESOLUTION_720 2
#define GCM_FLIP_VSYNC 1
#define SYSUTIL_EXIT_GAME 0x101
#define SYSUTIL_EVENT_SLOT0 0
#define SYSUTIL_DRAW_BEGIN 0x121
#define SYSUTIL_DRAW_END 0x122
#define SYSUTIL_MENU_OPEN 0x131
#define SYSUTIL_MENU_CLOSE 0x132

s32 gemGetState(u32, u32, system_time_t, gemState*);
s32 gemGetImageState(u32, gemImageState*);
s32 gemGetInertialState(u32, u32, system_time_t, gemInertialState*);
s32 gemGetInfo(gemInfo*);
s32 gemGetMemorySize(s32);
s32 gemInit(const gemAttribute*);
s32 gemEnd();
s32 gemPrepareCamera(s32, f32);
s32 gemReset(u32);
s32 gemUpdateStart(const void*, system_time_t);
s32 gemUpdateFinish();
s32 gemTrackHues(const u32*, u32*);
s32 gemCalibrate(u32);
s32 gemSetRumble(u32, u8);

s32 cameraInit();
s32 cameraEnd();
s32 cameraGetType(s32, cameraType*);
s32 cameraOpenEx(s32, cameraInfoEx*);
s32 cameraClose(s32);
s32 cameraStart(s32);
s32 cameraStop(s32);
s32 cameraReset(s32);
s32 cameraReadEx(s32, cameraReadInfo*);

s32 spursAttributeInitialize(SpursAttribute*, u8, s32, s32, bool);
s32 spursAttributeSetNamePrefix(SpursAttribute*, const char*, u32);
s32 spursInitializeWithAttribute(Spurs*, const SpursAttribute*);
s32 spursFinalize(Spurs*);
s32 sysSpuInitialize(u32, u32);
s32 sysThreadGetId(sys_ppu_thread_t*);
s32 sysThreadGetPriority(sys_ppu_thread_t, s32*);
s32 sysThreadCreate(sys_ppu_thread_t*, void (*)(void*), void*, s32, u64, u64, char*);
s32 sysThreadJoin(sys_ppu_thread_t, u64*);
void sysThreadExit(u64);
s32 sysMemContainerCreate(sys_mem_container_t*, std::size_t);
s32 sysMemContainerDestroy(sys_mem_container_t);
s32 sysGetCurrentTime(u64*, u64*);

s32 sysEventQueueReceive(sys_event_queue_t, sys_event_t*, u64);
s32 sysEventQueueDrain(sys_event_queue_t);
s32 sysEventQueueDestroy(sys_event_queue_t, s32);

s32 sysModuleLoad(s32);
s32 sysModuleUnload(s32);

s32 audioInit();
s32 audioQuit();
s32 audioPortOpen(audioPortParam*, u32*);
s32 audioPortStart(u32);
s32 audioPortStop(u32);
s32 audioGetPortConfig(u32, audioPortConfig*);
s32 audioPortClose(u32);
s32 audioCreateNotifyEventQueue(sys_event_queue_t*, sys_ipc_key_t*);
s32 audioSetNotifyEventQueue(sys_ipc_key_t);
s32 audioRemoveNotifyEventQueue(sys_ipc_key_t);

int ov_fopen(const char*, OggVorbis_File*);
vorbis_info* ov_info(OggVorbis_File*, int);
double ov_time_total(OggVorbis_File*, int);
long ov_read(OggVorbis_File*, char*, int, int, int, int, int*);
int ov_clear(OggVorbis_File*);
int ov_time_seek(OggVorbis_File*, double);

s32 rsxInit(gcmContextData**, u32, u32, const void*);
void* rsxMemalign(std::size_t, std::size_t);
void rsxFree(void*);
s32 rsxAddressToOffset(const void*, u32*);
void rsxFinish(gcmContextData*, u32);
void rsxFlushBuffer(gcmContextData*);
s32 gcmSetDisplayBuffer(u32, u32, u32, u32, u32);
void gcmSetFlipMode(u32);
void gcmResetFlipStatus();
u32 gcmGetFlipStatus();
s32 gcmSetFlip(gcmContextData*, u32);
void gcmSetWaitFlip(gcmContextData*);

s32 videoGetState(u32, u32, videoState*);
s32 videoGetResolution(u32, videoResolution*);
s32 videoGetResolutionAvailability(u32, u32, u32, u32);
s32 videoConfigure(u32, videoConfiguration*, void*, u32);
s32 sysUtilRegisterCallback(u32, void (*)(u64, u64, void*), void*);
s32 sysUtilUnregisterCallback(u32);
void sysUtilCheckCallback();

#endif
