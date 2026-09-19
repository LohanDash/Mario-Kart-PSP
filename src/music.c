#include <stdio.h>
#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>

#include "music.h"

#define PCM_SAMPLES 1024

static short mono_buffer[PCM_SAMPLES] __attribute__((aligned(64)));
static short stereo_buffer[PCM_SAMPLES * 2] __attribute__((aligned(64)));
static volatile int requested_track = MUSIC_NONE;
static volatile int music_running = 0;
static SceUID music_thread_id = -1;

static const char *track_path(enum MusicTrack track) {
    const char *root = "ms0:/PSP/GAME/MarioKartPSP/data/music/psp/";
    static char path[128];
    const char *name = NULL;
    switch (track) {
        case MUSIC_MAIN_MENU: name = "main_menu.pcm"; break;
        case MUSIC_SINGLE_MENU: name = "single_menu.pcm"; break;
        case MUSIC_TIME_TRIAL_START: name = "time_trial_start.pcm"; break;
        case MUSIC_MARIO_CIRCUIT: name = "mario_circuit.pcm"; break;
        case MUSIC_FINAL_LAP_JINGLE: name = "final_lap_jingle.pcm"; break;
        case MUSIC_MARIO_CIRCUIT_FINAL: name = "mario_circuit_final.pcm"; break;
        case MUSIC_WALUIGI_PINBALL: name = "waluigi_pinball.pcm"; break;
        case MUSIC_WALUIGI_FINAL_LAP_JINGLE: name = "final_lap_jingle.pcm"; break;
        case MUSIC_WALUIGI_PINBALL_FINAL: name = "waluigi_pinball_final.pcm"; break;
        case MUSIC_LUIGIS_MANSION: name = "luigis_mansion.pcm"; break;
        case MUSIC_LUIGIS_FINAL_LAP_JINGLE: name = "final_lap_jingle.pcm"; break;
        case MUSIC_LUIGIS_MANSION_FINAL: name = "luigis_mansion_final.pcm"; break;
        case MUSIC_TIME_TRIAL_RESULTS: name = "time_trial_results.pcm"; break;
        default: return NULL;
    }
    snprintf(path, sizeof(path), "%s%s", root, name);
    return path;
}

static int play_track(enum MusicTrack track, int channel) {
    const char *path = track_path(track);
    int fd;
    if (!path) return 0;
    fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd < 0) return 0;
    while (music_running && requested_track == (int)track) {
        int read = sceIoRead(fd, mono_buffer, sizeof(mono_buffer));
        int samples, i;
        if (read <= 0) {
            if (track == MUSIC_FINAL_LAP_JINGLE ||
                track == MUSIC_WALUIGI_FINAL_LAP_JINGLE ||
                track == MUSIC_LUIGIS_FINAL_LAP_JINGLE) break;
            sceIoLseek32(fd, 0, PSP_SEEK_SET);
            continue;
        }
        samples = read / (int)sizeof(short);
        for (i = 0; i < samples; ++i) {
            stereo_buffer[i * 2] = mono_buffer[i];
            stereo_buffer[i * 2 + 1] = mono_buffer[i];
        }
        for (; i < PCM_SAMPLES; ++i) {
            stereo_buffer[i * 2] = 0;
            stereo_buffer[i * 2 + 1] = 0;
        }
        sceAudioOutputPannedBlocking(channel, PSP_AUDIO_VOLUME_MAX,
                                     PSP_AUDIO_VOLUME_MAX, stereo_buffer);
    }
    sceIoClose(fd);
    return 1;
}

static int music_thread(SceSize args, void *argp) {
    int channel;
    (void)args; (void)argp;
    channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, PCM_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    if (channel < 0) return 0;
    while (music_running) {
        enum MusicTrack track = (enum MusicTrack)requested_track;
        if (track == MUSIC_NONE) sceKernelDelayThread(10000);
        else if (!play_track(track, channel)) sceKernelDelayThread(100000);
        if (requested_track == (int)track) {
            if (track == MUSIC_FINAL_LAP_JINGLE)
                requested_track = MUSIC_MARIO_CIRCUIT_FINAL;
            else if (track == MUSIC_WALUIGI_FINAL_LAP_JINGLE)
                requested_track = MUSIC_WALUIGI_PINBALL_FINAL;
            else if (track == MUSIC_LUIGIS_FINAL_LAP_JINGLE)
                requested_track = MUSIC_LUIGIS_MANSION_FINAL;
        }
    }
    sceAudioChRelease(channel);
    return 0;
}

void music_init(void) {
    music_running = 1;
    music_thread_id = sceKernelCreateThread("mkpsp_music", music_thread, 0x18, 0x10000, 0, NULL);
    if (music_thread_id >= 0) sceKernelStartThread(music_thread_id, 0, NULL);
}

void music_request(enum MusicTrack track) {
    requested_track = track;
}

void music_shutdown(void) {
    music_running = 0;
    requested_track = MUSIC_NONE;
    if (music_thread_id >= 0) sceKernelWaitThreadEnd(music_thread_id, NULL);
}
