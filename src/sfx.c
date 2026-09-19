#include <stdio.h>
#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>

#include "sfx.h"

#define SFX_SAMPLES 1024

#define SFX_VOICES 3
static short mono[SFX_VOICES][SFX_SAMPLES] __attribute__((aligned(64)));
static short stereo[SFX_VOICES][SFX_SAMPLES * 2] __attribute__((aligned(64)));
static short engine_mono[SFX_SAMPLES] __attribute__((aligned(64)));
static short engine_stereo[SFX_SAMPLES * 2] __attribute__((aligned(64)));
static short drift_mono[SFX_SAMPLES] __attribute__((aligned(64)));
static short drift_stereo[SFX_SAMPLES * 2] __attribute__((aligned(64)));
static volatile int running;
static volatile int requested[SFX_VOICES];
static volatile unsigned int request_serial[SFX_VOICES];
static volatile unsigned int next_voice;
static volatile int engine_enabled;
static volatile int drift_enabled;
static SceUID one_shot_thread_id[SFX_VOICES] = {-1, -1, -1};
static SceUID engine_thread_id = -1, drift_thread_id = -1;
static int voice_index[SFX_VOICES] = {0, 1, 2};

static const char *path_for(enum SfxId id) {
#define SFX_PATH(name) "ms0:/PSP/GAME/MarioKartPSP/data/sfx/psp/" name
    switch (id) {
        case SFX_MENU_MOVE: return SFX_PATH("menu_move.pcm");
        case SFX_MENU_CONFIRM: return SFX_PATH("menu_confirm.pcm");
        case SFX_MENU_BACK: return SFX_PATH("menu_back.pcm");
        case SFX_RACE_START: return SFX_PATH("race_start.pcm");
        case SFX_DRIFT: return SFX_PATH("drift.pcm");
        case SFX_DRIFT_BOOST: return SFX_PATH("drift_boost.pcm");
        case SFX_ITEM: return SFX_PATH("mario_item.pcm");
        case SFX_BOOST: return SFX_PATH("mario_boost.pcm");
        case SFX_CANNON: return SFX_PATH("cannon.pcm");
        case SFX_LANDING: return SFX_PATH("landing.pcm");
        case SFX_WALL_HIT: return SFX_PATH("wall_hit.pcm");
        case SFX_HOP: return SFX_PATH("mario_hop.pcm");
        case SFX_PINBALL_LAUNCH: return SFX_PATH("pinball_launch.pcm");
        case SFX_PINBALL_DASH: return SFX_PATH("pinball_dash.pcm");
        case SFX_PINBALL_BUMPER: return SFX_PATH("pinball_bumper.pcm");
        case SFX_PINBALL_FLIPPER: return SFX_PATH("pinball_flipper.pcm");
        case SFX_PINBALL_BALL_HIT: return SFX_PATH("pinball_ball_hit.pcm");
        case SFX_PINBALL_EXIT: return SFX_PATH("pinball_exit.pcm");
        case SFX_LAP: return SFX_PATH("lap.pcm");
        case SFX_RACE_FINISH: return SFX_PATH("race_finish.pcm");
        case SFX_MANSION_TREE: return SFX_PATH("mansion_tree.pcm");
        case SFX_MANSION_CHANDELIER: return SFX_PATH("mansion_chandelier.pcm");
        case SFX_MANSION_BOO: return SFX_PATH("mansion_boo.pcm");
        case SFX_MANSION_PICTURE: return SFX_PATH("mansion_picture.pcm");
        case SFX_MARIO_0: return SFX_PATH("mario_0.pcm");
        case SFX_MARIO_1: return SFX_PATH("mario_1.pcm");
        case SFX_MARIO_2: return SFX_PATH("mario_2.pcm");
        case SFX_MARIO_3: return SFX_PATH("mario_3.pcm");
        case SFX_MARIO_4: return SFX_PATH("mario_4.pcm");
        case SFX_MARIO_5: return SFX_PATH("mario_5.pcm");
        case SFX_MARIO_6: return SFX_PATH("mario_6.pcm");
        case SFX_MARIO_7: return SFX_PATH("mario_7.pcm");
        case SFX_MARIO_8: return SFX_PATH("mario_8.pcm");
        case SFX_MARIO_9: return SFX_PATH("mario_9.pcm");
        case SFX_MARIO_10: return SFX_PATH("mario_10.pcm");
        default: return NULL;
    }
#undef SFX_PATH
}

static int one_shot_thread(SceSize args, void *argp) {
    int voice = *(int *)argp;
    int channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, SFX_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    unsigned int played_serial = 0;
    (void)args; (void)argp;
    if (channel < 0) return 0;
    while (running) {
        if (played_serial == request_serial[voice] || requested[voice] == SFX_NONE) {
            sceKernelDelayThread(2000);
        } else {
            unsigned int serial = request_serial[voice];
            const char *path = path_for((enum SfxId)requested[voice]);
            int fd = path ? sceIoOpen(path, PSP_O_RDONLY, 0777) : -1;
            played_serial = serial;
            while (running && serial == request_serial[voice] && fd >= 0) {
                int bytes = sceIoRead(fd, mono[voice], sizeof(mono[voice]));
                int samples, i;
                if (bytes <= 0) break;
                samples = bytes / 2;
                for (i = 0; i < samples; ++i)
                    stereo[voice][i * 2] = stereo[voice][i * 2 + 1] = mono[voice][i];
                for (; i < SFX_SAMPLES; ++i)
                    stereo[voice][i * 2] = stereo[voice][i * 2 + 1] = 0;
                /* Several hardware channels are mixed together. Keep enough
                   headroom for three effects, engine and music without clipping. */
                sceAudioOutputPannedBlocking(channel, 0x3800, 0x3800, stereo[voice]);
            }
            if (fd >= 0) sceIoClose(fd);
        }
    }
    sceAudioChRelease(channel);
    return 0;
}

static int engine_thread(SceSize args, void *argp) {
    int channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, SFX_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    (void)args; (void)argp;
    if (channel < 0) return 0;
    while (running) {
        int fd;
        if (!engine_enabled) { sceKernelDelayThread(10000); continue; }
        fd = sceIoOpen("ms0:/PSP/GAME/MarioKartPSP/data/sfx/psp/engine.pcm", PSP_O_RDONLY, 0777);
        if (fd < 0) { sceKernelDelayThread(100000); continue; }
        while (running && engine_enabled) {
            int bytes = sceIoRead(fd, engine_mono, sizeof(engine_mono));
            int samples, i;
            if (bytes <= 0) { sceIoLseek32(fd, 0, PSP_SEEK_SET); continue; }
            samples = bytes / 2;
            for (i = 0; i < samples; ++i)
                engine_stereo[i * 2] = engine_stereo[i * 2 + 1] = engine_mono[i];
            for (; i < SFX_SAMPLES; ++i)
                engine_stereo[i * 2] = engine_stereo[i * 2 + 1] = 0;
            sceAudioOutputPannedBlocking(channel, 0x1800, 0x1800, engine_stereo);
        }
        sceIoClose(fd);
    }
    sceAudioChRelease(channel);
    return 0;
}

static int drift_thread(SceSize args, void *argp) {
    int channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, SFX_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    (void)args; (void)argp;
    if (channel < 0) return 0;
    while (running) {
        int fd;
        if (!drift_enabled) { sceKernelDelayThread(10000); continue; }
        /* Open once per drift, then loop from the already-open stream.  The old
           one-shot path reopened the Memory Stick every nine frames. */
        fd = sceIoOpen("ms0:/PSP/GAME/MarioKartPSP/data/sfx/psp/drift.pcm", PSP_O_RDONLY, 0777);
        if (fd < 0) { sceKernelDelayThread(100000); continue; }
        while (running && drift_enabled) {
            int bytes = sceIoRead(fd, drift_mono, sizeof(drift_mono));
            int samples, i;
            if (bytes <= 0) { sceIoLseek32(fd, 0, PSP_SEEK_SET); continue; }
            samples = bytes / 2;
            for (i = 0; i < samples; ++i)
                drift_stereo[i * 2] = drift_stereo[i * 2 + 1] = drift_mono[i];
            for (; i < SFX_SAMPLES; ++i)
                drift_stereo[i * 2] = drift_stereo[i * 2 + 1] = 0;
            sceAudioOutputPannedBlocking(channel, 0x2400, 0x2400, drift_stereo);
        }
        sceIoClose(fd);
    }
    sceAudioChRelease(channel);
    return 0;
}

void sfx_init(void) {
    int i;
    running = 1; next_voice = 0; engine_enabled = 0; drift_enabled = 0;
    for (i = 0; i < SFX_VOICES; ++i) {
        requested[i] = SFX_NONE; request_serial[i] = 0;
        one_shot_thread_id[i] = sceKernelCreateThread("mkpsp_sfx", one_shot_thread, 0x17, 0x4000, 0, NULL);
    }
    engine_thread_id = sceKernelCreateThread("mkpsp_engine", engine_thread, 0x18, 0x4000, 0, NULL);
    drift_thread_id = sceKernelCreateThread("mkpsp_drift", drift_thread, 0x18, 0x4000, 0, NULL);
    for (i = 0; i < SFX_VOICES; ++i)
        if (one_shot_thread_id[i] >= 0)
            sceKernelStartThread(one_shot_thread_id[i], sizeof(int), &voice_index[i]);
    if (engine_thread_id >= 0) sceKernelStartThread(engine_thread_id, 0, NULL);
    if (drift_thread_id >= 0) sceKernelStartThread(drift_thread_id, 0, NULL);
}

void sfx_play(enum SfxId id) {
    unsigned int voice = next_voice++ % SFX_VOICES;
    requested[voice] = id;
    ++request_serial[voice];
}
void sfx_set_engine(int enabled) { engine_enabled = enabled; }
void sfx_set_drift(int enabled) { drift_enabled = enabled; }

void sfx_shutdown(void) {
    int i;
    running = 0; engine_enabled = 0; drift_enabled = 0;
    for (i = 0; i < SFX_VOICES; ++i) ++request_serial[i];
    for (i = 0; i < SFX_VOICES; ++i)
        if (one_shot_thread_id[i] >= 0) sceKernelWaitThreadEnd(one_shot_thread_id[i], NULL);
    if (engine_thread_id >= 0) sceKernelWaitThreadEnd(engine_thread_id, NULL);
    if (drift_thread_id >= 0) sceKernelWaitThreadEnd(drift_thread_id, NULL);
}
