/* RetroArch - A frontend for libretro.
 *
 * Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 * Copyright (C) 2011-2017 - Daniel De Matteis
 *
 * RetroArch is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with RetroArch.
 * If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * MIAO audio driver for miyoomini with audioserver support (audiofix parameter)
 * The name "audioio" is used to minimize the number of files to be rewritten as much as possible,
 * but /dev/audio is not used.
 * 
 * FIXED VERSION - Corrected issues with noise, quality, and volume control
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

/* MI_AO and MI_SYS library headers - CRITICAL for MI_AO functionality */
#include <mi_sys.h>
#include <mi_ao.h>

#define YIELD_WAIT /* Flag to wait with sched_yield() when the wait time is less than 10ms */

#include <sched.h>

#ifdef YIELD_WAIT
#include <sched.h>
#ifndef SCHED_IDLE
#define SCHED_IDLE 5
#endif
#endif

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

#include "../audio_driver.h"
#include "../../verbosity.h"
#include "volume/volume.h"

/* MI_AO_SendFrame Max bytes */
#define MIAO_MAX_BUFSIZE 51200
#define AUDIOSERVER_FIFO "/tmp/audio_fifo_server"
#define AUDIOSERVER_FIFO_IOCTL_REQ "/tmp/audio_fifo_ioctl_req"
#define AUDIOSERVER_FIFO_IOCTL_RES "/tmp/audio_fifo_ioctl_res"

typedef struct miao_audio
{
    MI_AUDIO_Frame_t AoSendFrame;
    size_t bufsize;
    uint32_t freq;
    bool nonblock;
    bool is_paused;
    void* nullbuf;

    /* audiofix mode support */
    bool audiofix_mode;   /* TRUE=FIFO (audiofix=1), FALSE=hardware (audiofix=0) */
    int fifo_fd;          /* FIFO file descriptor */
    int ioctl_req_fd;     /* ioctl request FIFO */
    int ioctl_res_fd;     /* ioctl response FIFO */
} miao_audio_t;

/**
 * Initialize FIFO mode for audioserver communication
 */
static int miao_init_fifo(miao_audio_t *miaoaudio)
{
    int flags;

    miaoaudio->fifo_fd = open(AUDIOSERVER_FIFO, O_WRONLY | O_NONBLOCK);
    if (miaoaudio->fifo_fd < 0)
    {
        RARCH_ERR("[MIAO]: Failed to open audio FIFO: %s\n", strerror(errno));
        return -1;
    }

    miaoaudio->ioctl_req_fd = open(AUDIOSERVER_FIFO_IOCTL_REQ, O_WRONLY | O_NONBLOCK);
    miaoaudio->ioctl_res_fd = open(AUDIOSERVER_FIFO_IOCTL_RES, O_RDONLY | O_NONBLOCK);

    if (!miaoaudio->nonblock)
    {
        flags = fcntl(miaoaudio->fifo_fd, F_GETFL, 0);
        if (flags >= 0)
            fcntl(miaoaudio->fifo_fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    RARCH_LOG("[MIAO]: FIFO mode initialized (audiofix=1)\n");
    return 0;
}

/**
 * Write audio data to FIFO with proper timing and buffer management
 * FIXED: Implementa el mismo control de buffer que el modo hardware
 */
static ssize_t miao_write_fifo(miao_audio_t *miaoaudio, const void *buf, size_t size)
{
    ssize_t written = 0;
    ssize_t result;
    const char *ptr = (const char *)buf;
    int retry_count = 0;
    const int MAX_RETRIES = 100;

    while (written < (ssize_t)size)
    {
        result = write(miaoaudio->fifo_fd, ptr + written, size - written);

        if (result < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                if (miaoaudio->nonblock)
                    return written > 0 ? written : 0;

                retry_count++;
                if (retry_count >= MAX_RETRIES)
                {
                    RARCH_WARN("[MIAO]: FIFO write timeout\n");
                    return written > 0 ? written : -1;
                }

                /* FIXED: Usar el mismo timing que el modo hardware */
                usleep(1000);
                continue;
            }
            else
            {
                RARCH_ERR("[MIAO]: FIFO write error: %s\n", strerror(errno));
                return -1;
            }
        }

        written += result;
        retry_count = 0;
    }

    return written;
}

static void *miao_init(const char *device,
        unsigned rate, unsigned latency,
        unsigned block_frames,
        unsigned *new_rate)
{
    MI_AUDIO_Attr_t attr;
    uint32_t samples;
    miao_audio_t *miaoaudio = (miao_audio_t*)calloc(1, sizeof(miao_audio_t));

    if (!miaoaudio)
        return NULL;

    /* FIXED: Inicializar file descriptors a -1 */
    miaoaudio->fifo_fd = -1;
    miaoaudio->ioctl_req_fd = -1;
    miaoaudio->ioctl_res_fd = -1;

    const int freqtable[] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 };

    for (uint32_t i = 0; i < (sizeof(freqtable)/sizeof(int)); i++)
    {
        if (rate <= freqtable[i])
        {
            miaoaudio->freq = freqtable[i];
            break;
        }
    }
    if (rate > 48000)
        miaoaudio->freq = 48000;

    if (miaoaudio->freq != rate)
    {
        *new_rate = miaoaudio->freq;
        RARCH_WARN("[MIAO]: Requested sample rate not supported, adjusting output rate to %d Hz.\n", *new_rate);
    }

    miaoaudio->bufsize = (latency * miaoaudio->freq / 1000) << 2;
    miaoaudio->bufsize = (miaoaudio->bufsize + 15) & ~15;

    if (miaoaudio->bufsize == 0)
        miaoaudio->bufsize = 16;
    else if (miaoaudio->bufsize > MIAO_MAX_BUFSIZE)
        miaoaudio->bufsize = MIAO_MAX_BUFSIZE;

    RARCH_LOG("[MIAO]: Requested %u ms latency, got %.2f ms\n",
            latency, (float)((miaoaudio->bufsize >> 2) * 1000 / miaoaudio->freq));

    /* Read audiofix parameter from system.json */
    int audiofix_value = getValueMM("audiofix");
    miaoaudio->audiofix_mode = (audiofix_value == 1);
    RARCH_LOG("[MIAO]: audiofix from system.json = %d\n", audiofix_value);

    if (miaoaudio->audiofix_mode)
    {
        /* FIFO mode - do NOT call MI_AO functions */
        RARCH_LOG("[MIAO]: Using FIFO mode (audiofix=1)\n");

        if (miao_init_fifo(miaoaudio) != 0)
        {
            RARCH_ERR("[MIAO]: FIFO initialization failed\n");
            goto error;
        }

        /* FIXED: Añadir control de volumen para audiofix=1 */
        int target_vol = getVolumeMM();
        set_snd_level(target_vol);
        RARCH_LOG("[MIAO]: Volume set to %ddB (audiofix=1)\n", target_vol);

        return miaoaudio;
    }

    /* Hardware mode - direct MI_AO access (original code path) */
    RARCH_LOG("[MIAO]: Using hardware mode (audiofix=0)\n");

    samples = miaoaudio->bufsize >> 2;
    if (samples > 2048)
        samples = 2048;

    memset(&attr, 0, sizeof(attr));
    attr.eSamplerate = (MI_AUDIO_SampleRate_e)miaoaudio->freq;
    attr.eSoundmode = E_MI_AUDIO_SOUND_MODE_STEREO;
    attr.u32ChnCnt = 2;
    attr.u32PtNumPerFrm = samples;

    /* Maybe unnecessary but just in case */
    miaoaudio->AoSendFrame.eSoundmode = E_MI_AUDIO_SOUND_MODE_STEREO;

    if (MI_AO_SetPubAttr(0, &attr))
        goto error;
    if (MI_AO_Enable(0))
        goto error;
    if (MI_AO_EnableChn(0, 0))
        goto error;
    if (MI_AO_SetMute(0, FALSE))
        goto error;

    /* FIXED: Añadir control de volumen para audiofix=0 (igual que audioio_miyoomini.c original) */
    int volumeMM = setVolumeMM();
    char command[100];
    sprintf(command, "tinymix set 6 %d", volumeMM);
    system(command);

    int target_vol = getVolumeMM();
    set_snd_level(target_vol);
    RARCH_LOG("[MIAO]: Volume set (tinymix + %ddB) (audiofix=0)\n", target_vol);

    /* Send pre-fill null data */
    miaoaudio->nullbuf = calloc(1, miaoaudio->bufsize);
    if (!miaoaudio->nullbuf)
        goto error;

    miaoaudio->AoSendFrame.apVirAddr[0] = miaoaudio->nullbuf;
    miaoaudio->AoSendFrame.u32Len = miaoaudio->bufsize;
    MI_AO_ClearChnBuf(0, 0);
    MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);

    return miaoaudio;

error:
    if (miaoaudio->fifo_fd >= 0)
        close(miaoaudio->fifo_fd);
    if (miaoaudio->ioctl_req_fd >= 0)
        close(miaoaudio->ioctl_req_fd);
    if (miaoaudio->ioctl_res_fd >= 0)
        close(miaoaudio->ioctl_res_fd);
    free(miaoaudio->nullbuf);
    free(miaoaudio);
    RARCH_ERR("[MIAO]: Failed to initialize...\n");
    return NULL;
}

static ssize_t miao_write(void *data, const void *buf, size_t size)
{
    miao_audio_t *miaoaudio = (miao_audio_t*)data;

    if ((!size) || (miaoaudio->is_paused))
        return 0;

    /* FIFO mode */
    if (miaoaudio->audiofix_mode)
        return miao_write_fifo(miaoaudio, buf, size);

    /* Hardware mode - FIXED: código idéntico al original audioio_miyoomini.c */
    miaoaudio->AoSendFrame.apVirAddr[0] = (void*)buf;

    ssize_t write_bytes;
    uint32_t usleepclock;
    MI_AO_ChnState_t status;

    MI_AO_QueryChnStat(0, 0, &status);
    int avail = miaoaudio->bufsize - status.u32ChnBusyNum;

    /* FIXED: sin cast a (int)size como en el original */
    if ((avail < size) && (!miaoaudio->nonblock))
    {
        write_bytes = size;
        miaoaudio->AoSendFrame.u32Len = write_bytes;
        MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);

        /* wait process for miyoomini with 10ms sleep precision */
        MI_AO_QueryChnStat(0, 0, &status);
        if (status.u32ChnBusyNum > miaoaudio->bufsize)
        {
            usleepclock = (uint64_t)(status.u32ChnBusyNum - miaoaudio->bufsize) * 1000000 / (miaoaudio->freq << 2);

#ifndef YIELD_WAIT
            if (usleepclock)
                usleep(usleepclock);
#else
            if (usleepclock > 0x2800)
                usleep(usleepclock - 0x2800); /* 0.24ms margin */

            const struct sched_param scprm = {0};
            int policy = sched_getscheduler(0);
            sched_setscheduler(0, SCHED_IDLE, &scprm);

            do {
                sched_yield();
                MI_AO_QueryChnStat(0, 0, &status);
            } while (status.u32ChnBusyNum > miaoaudio->bufsize);

            sched_setscheduler(0, policy, &scprm);
#endif
        }
    }
    else
    {
        write_bytes = avail > size ? size : avail;
        if (write_bytes > 0)
        {
            miaoaudio->AoSendFrame.u32Len = write_bytes;
            MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);
        }
        else
            return 0;
    }

    return write_bytes;
}

static bool miao_stop(void *data)
{
    miao_audio_t *miaoaudio = (miao_audio_t*)data;

    if (miaoaudio->is_paused)
        return true;

    RARCH_LOG("[MIAO audio]: Pausing.\n");
    miaoaudio->is_paused = true;

    /* FIFO mode: flush remaining data by closing and reopening FIFO */
    if (miaoaudio->audiofix_mode)
    {
        /* Close FIFO to signal pause to audioserver */
        if (miaoaudio->fifo_fd >= 0)
        {
            close(miaoaudio->fifo_fd);
            miaoaudio->fifo_fd = -1;
        }
        RARCH_LOG("[MIAO]: FIFO closed for pause (audiofix=1)\n");
    }

    return true;
}

static bool miao_start(void *data, bool is_shutdown)
{
    miao_audio_t *miaoaudio = (miao_audio_t*)data;

    if (!miaoaudio)
        return false;

    /* Prevents restarting audio when the menu
     * is toggled off on shutdown */
    if (is_shutdown)
        return true;

    if (miaoaudio->is_paused)
    {
        if (miaoaudio->audiofix_mode)
        {
            /* FIFO mode: reopen FIFO after pause */
            int flags;
            miaoaudio->fifo_fd = open(AUDIOSERVER_FIFO, O_WRONLY | O_NONBLOCK);

            if (miaoaudio->fifo_fd < 0)
            {
                RARCH_ERR("[MIAO]: Failed to reopen FIFO after pause: %s\n", strerror(errno));
                return false;
            }

            if (!miaoaudio->nonblock)
            {
                flags = fcntl(miaoaudio->fifo_fd, F_GETFL, 0);
                if (flags >= 0)
                    fcntl(miaoaudio->fifo_fd, F_SETFL, flags & ~O_NONBLOCK);
            }

            RARCH_LOG("[MIAO]: FIFO reopened after pause (audiofix=1)\n");
        }
        else
        {
            /* Hardware mode: Send pre-fill null data */
            miaoaudio->AoSendFrame.apVirAddr[0] = miaoaudio->nullbuf;
            miaoaudio->AoSendFrame.u32Len = miaoaudio->bufsize;
            MI_AO_ClearChnBuf(0, 0);
            MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);
        }

        miaoaudio->is_paused = false;
        RARCH_LOG("[MIAO audio]: Resumed.\n");
    }

    return true;
}

static bool miao_alive(void *data)
{
    miao_audio_t *miaoaudio = (miao_audio_t*)data;

    if (!miaoaudio)
        return false;

    return !miaoaudio->is_paused;
}

static void miao_set_nonblock_state(void *data, bool state)
{
    miao_audio_t *miaoaudio = (miao_audio_t*)data;
    miaoaudio->nonblock = state;
}

static void miao_free(void *data)
{
    miao_audio_t *miaoaudio = (miao_audio_t*)data;

    if (miaoaudio->audiofix_mode)
    {
        /* FIFO mode: close pipes */
        if (miaoaudio->fifo_fd >= 0)
            close(miaoaudio->fifo_fd);
        if (miaoaudio->ioctl_req_fd >= 0)
            close(miaoaudio->ioctl_req_fd);
        if (miaoaudio->ioctl_res_fd >= 0)
            close(miaoaudio->ioctl_res_fd);
    }
    else
    {
        /* Hardware mode: disable hardware */
        MI_AO_ClearChnBuf(0, 0);
        MI_AO_DisableChn(0, 0);
        MI_AO_Disable(0);
    }

    free(miaoaudio->nullbuf);
    free(data);
}

static bool miao_use_float(void *data)
{
    (void)data;
    return false;
}

static size_t miao_write_avail(void *data)
{
    miao_audio_t *miaoaudio = (miao_audio_t*)data;

    if (miaoaudio->audiofix_mode)
        return 1024; /* FIFO buffer estimate */

    MI_AO_ChnState_t status;
    MI_AO_QueryChnStat(0, 0, &status);
    int avail = MIAO_MAX_BUFSIZE - status.u32ChnBusyNum;
    return (avail > 0) ? avail : 0;
}

static size_t miao_buffer_size(void *data)
{
    miao_audio_t *miaoaudio = (miao_audio_t*)data;

    if (miaoaudio->audiofix_mode)
        return 1024; /* FIFO buffer size */

    return MIAO_MAX_BUFSIZE;
}

audio_driver_t audio_audioio = {
    miao_init,
    miao_write,
    miao_stop,
    miao_start,
    miao_alive,
    miao_set_nonblock_state,
    miao_free,
    miao_use_float,
    "audioio",
    NULL,
    NULL,
    miao_write_avail,
    miao_buffer_size,
};
