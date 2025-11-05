// LGPL-2.1 License
// (C) 2025 Steward Fu <steward.fu@gmail.com>

#ifndef __SDL_AUDIO_MINI_H__
#define __SDL_AUDIO_MINI_H__

#include "../../SDL_internal.h"
#include "../SDL_sysaudio.h"

#define _THIS SDL_AudioDevice *this

#define MI_AUDIO_SAMPLE_PER_FRAME   768
#define FUDGE_TICKS                 10
#define FREQ                        44100
#define CHANNELS                    2
#define MAX_VOLUME          20
#define MIN_RAW_VALUE       -60
#define MAX_RAW_VALUE       30
#define MI_AO_SETVOLUME     0x4008690b
#define MI_AO_GETVOLUME     0xc008690c
#define MI_AO_SETMUTE       0x4008690d
#define JSON_APP_FILE       "/appconfigs/system.json"
#define JSON_VOL_KEY        "vol"

struct SDL_PrivateAudioData {
    int mixlen;
    int audio_fd;
    uint8_t *mixbuf;
};

#endif

