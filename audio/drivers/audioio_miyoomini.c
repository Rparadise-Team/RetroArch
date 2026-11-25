/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

/*
      MIAO audio driver for miyoomini
      The name "audioio" is used to minimize the number of files to be rewritten as much as possible,
      but /dev/audio is not used.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define YIELD_WAIT
#include <mi_ao.h>
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
#include "../../gfx/drivers/miyoomini/miyoo_prof.h"

/* MI_AO_SendFrame Max bytes */
#define MIAO_MAX_BUFSIZE 51200

/* FIFO audioserver */
#define AUDIOSERVER_FIFO "/tmp/audio_fifo_server"

/* ---- Optional diagnostic: audio queue occupancy -------------------------
 * Only compiled when -DKORIKI_AUDIO_DIAG is added to the compiler flags,
 * and then only active when the environment has KORIKI_AUDIO_DIAG=1.
 * Every 2 s a line in /tmp/audiodiag.log:
 *   ms window_ms mode bytes_per_s writes blocked_ms samples queue_min queue_avg queue_max
 * (queue read right after each write, so it includes that write)
 * queue = bytes waiting: MI_AO channel busy bytes (direct) or bytes in the
 * audioserver FIFO (audiofix). 192 bytes = 1 ms at 48 kHz stereo S16. */
#include <time.h>
#include <sys/ioctl.h>
#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif
#ifndef F_GETPIPE_SZ
#define F_GETPIPE_SZ 1032
#endif
#define AUDIO_FRAME_BYTES 4
#define AUDIO_WAIT_US 10000  /* One tick on the Miyoo kernel (HZ=100). */
#ifdef KORIKI_AUDIO_DIAG
static FILE *diag_f;
static int64_t diag_t0, diag_bytes, diag_writes, diag_blocked_us, diag_qsum, diag_qn;
static void diag_reset(int64_t now);
static long diag_qmin = -1, diag_qmax;
static int64_t diag_now_us(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
static void diag_sample(long q)
{
   if (q < 0) return;
   if (diag_qmin < 0 || q < diag_qmin) diag_qmin = q;
   if (q > diag_qmax) diag_qmax = q;
   diag_qsum += q; diag_qn++;
}
static void diag_flush(int mode)
{
   int64_t now = diag_now_us();
   if (!diag_f) return;
   if (now - diag_t0 < 2000000) return;
   fprintf(diag_f, "%lld %lld %s %lld %lld %lld %lld %ld %lld %ld\n", (long long)(now / 1000),
         (long long)((now - diag_t0) / 1000), mode ? "fifo" : "miao",
         (long long)(diag_bytes * 1000000 / (now - diag_t0)),
         (long long)diag_writes, (long long)(diag_blocked_us / 1000), (long long)diag_qn, diag_qmin,
         (long long)(diag_qn ? diag_qsum / diag_qn : -1), diag_qn ? diag_qmax : -1);
   fflush(diag_f);
   diag_reset(now);
}
static void diag_reset(int64_t now)
{
   diag_t0 = now; diag_bytes = diag_writes = diag_blocked_us = diag_qsum = diag_qn = 0;
   diag_qmin = -1; diag_qmax = 0;
}
#endif

typedef struct miao_audio
{
   MI_AUDIO_Frame_t AoSendFrame;
   size_t bufsize;
   size_t queue_limit;
   size_t prime_size;
   int pipe_size;
   bool pipe_nonblock;
   uint32_t freq;
   bool nonblock;
   bool is_paused;

   bool audioserver_mode;
   int audioserver_fd;

   void* nullbuf;
} miao_audio_t;

/* Occupancy is in stereo S16 bytes on either path. In FIFO mode this
 * deliberately measures only the queue owned by this writer. */
static long miao_queued(const miao_audio_t *audio)
{
   if (audio->audioserver_mode) {
      int bytes;
      return ioctl(audio->audioserver_fd, FIONREAD, &bytes) == 0 ? bytes : -1;
   }

   MI_AO_ChnState_t status;
   return MI_AO_QueryChnStat(0, 0, &status) == 0 ?
         (long)status.u32ChnBusyNum : -1;
}

static void miao_prime(miao_audio_t *audio)
{
   audio->AoSendFrame.apVirAddr[0] = audio->nullbuf;
   audio->AoSendFrame.u32Len = audio->prime_size;
   MI_AO_SendFrame(0, 0, &audio->AoSendFrame, 0);
}

static void *miao_init(const char *device,
      unsigned rate, unsigned latency,
      unsigned block_frames,
      unsigned *new_rate)
{
   MI_AUDIO_Attr_t attr;
   uint32_t samples;

   miao_audio_t *miaoaudio = (miao_audio_t*)calloc(1, sizeof(miao_audio_t));
   if (!miaoaudio) return NULL;

   int audiofix = getValueMM("audiofix");
   miaoaudio->audioserver_mode = (audiofix != 0);
   miaoaudio->audioserver_fd = -1;
   miaoaudio->pipe_size = -1;

   const int freqtable[] = { 8000,11025,12000,16000,22050,24000,32000,44100,48000 };
   for (uint32_t i=0; i<(sizeof(freqtable)/sizeof(int)); i++) {
      if (rate <= freqtable[i]) { miaoaudio->freq = freqtable[i]; break; }
   } 
   if (rate > 48000) miaoaudio->freq = 48000;

   if (miaoaudio->freq != rate) {
      *new_rate = miaoaudio->freq;
      RARCH_WARN("[MIAO]: Requested rate adjusted to %d Hz.\n", *new_rate);
   }

   const uint64_t bytes = (uint64_t)latency * miaoaudio->freq / 1000 * AUDIO_FRAME_BYTES;
   miaoaudio->bufsize = bytes > MIAO_MAX_BUFSIZE ?
         MIAO_MAX_BUFSIZE : ((size_t)bytes + 15) & ~(size_t)15;
   /* Do not make a single tick of scheduling jitter larger than the
    * whole queue, even when a very small latency was requested. */
   const size_t tick_bytes = (miaoaudio->freq / 100) * AUDIO_FRAME_BYTES;
   if (miaoaudio->bufsize < tick_bytes)
      miaoaudio->bufsize = tick_bytes;
   miaoaudio->queue_limit = miaoaudio->bufsize;
   miaoaudio->prime_size = (miaoaudio->bufsize / 2) & ~(size_t)(AUDIO_FRAME_BYTES - 1);

   samples = miaoaudio->bufsize >> 2;
   if (samples > 2048) samples = 2048;

   miaoaudio->nullbuf = calloc(1, miaoaudio->bufsize);
   if (!miaoaudio->nullbuf) goto error;

   if (miaoaudio->audioserver_mode) {
      miaoaudio->audioserver_fd = open(AUDIOSERVER_FIFO, O_WRONLY);
      if (miaoaudio->audioserver_fd < 0) {
         RARCH_ERR("[MIAO]: Cannot open audioserver FIFO.\n");
         goto error;
      }
      /* Bound the kernel queue even if the server left the default 64 KiB
       * pipe. FIONREAD below enforces the smaller, unrounded budget. */
      const int granted = fcntl(miaoaudio->audioserver_fd, F_SETPIPE_SZ,
                               (int)miaoaudio->bufsize);
      miaoaudio->pipe_size = fcntl(miaoaudio->audioserver_fd, F_GETPIPE_SZ);
      if (miaoaudio->pipe_size < 0)
         miaoaudio->pipe_size = granted;
      if (granted < 0)
         RARCH_WARN("[MIAO]: Could not resize audioserver FIFO; using occupancy limit.\n");

      /* Leave half the latency budget downstream of the FIFO. Break up
       * writes rather than waiting for a whole batch to fit in that half. */
      miaoaudio->queue_limit = miaoaudio->prime_size;
      if (miaoaudio->queue_limit < tick_bytes)
         miaoaudio->queue_limit = tick_bytes;
      if (miaoaudio->pipe_size > 0 &&
          miaoaudio->queue_limit > (size_t)miaoaudio->pipe_size)
         miaoaudio->queue_limit = (size_t)miaoaudio->pipe_size;
      RARCH_LOG("[MIAO]: With audioserver\n");
   }
   else
   {
      memset(&attr, 0, sizeof(attr));
      attr.eSamplerate = (MI_AUDIO_SampleRate_e)miaoaudio->freq;
      attr.eSoundmode  = E_MI_AUDIO_SOUND_MODE_STEREO;
      attr.u32ChnCnt   = 2;
      attr.u32PtNumPerFrm = samples;

      miaoaudio->AoSendFrame.eSoundmode = E_MI_AUDIO_SOUND_MODE_STEREO;

      if (MI_AO_SetPubAttr(0,&attr)) goto error;
      if (MI_AO_Enable(0)) goto error;
      if (MI_AO_EnableChn(0,0)) goto error;
      if (MI_AO_SetMute(0,FALSE)) goto error;

      MI_AO_ClearChnBuf(0,0);
      miao_prime(miaoaudio);
      RARCH_LOG("[MIAO]: Whitout audioserver\n");
   }

   int target_vol = getVolumeMM();
   set_snd_level(target_vol);
#ifdef KORIKI_AUDIO_DIAG
   const char *diag = getenv("KORIKI_AUDIO_DIAG");
   if (diag && !strcmp(diag, "1")) {
      diag_f = fopen("/tmp/audiodiag.log", "a");
      if (diag_f) {
         const int64_t now = diag_now_us();
         diag_reset(now);
         fprintf(diag_f, "# init %lld ms: mode=%s rate=%u freq=%u latency=%u ms bufsize=%zu limit=%zu pipe=%d\n",
               (long long)(now / 1000), miaoaudio->audioserver_mode ? "fifo" : "miao",
               rate, miaoaudio->freq, latency, miaoaudio->bufsize,
               miaoaudio->queue_limit, miaoaudio->pipe_size);
         fflush(diag_f);
      }
   }
#endif
   return miaoaudio;

error:
   if (miaoaudio->audioserver_fd >= 0) close(miaoaudio->audioserver_fd);
   free(miaoaudio->nullbuf);
   free(miaoaudio);
   return NULL;
}

static ssize_t miao_write_impl(void *data, const void *buf, size_t size)
{
   miao_audio_t *audio = (miao_audio_t*)data;
   size_t total = 0;
   size &= ~(size_t)(AUDIO_FRAME_BYTES - 1);
   if (audio->is_paused)
      return 0;

   while (total < size) {
      const long queued = miao_queued(audio);
      size_t count = size - total;
      if (count > audio->queue_limit)
         count = audio->queue_limit;
      if (queued >= 0) {
         size_t room = (size_t)queued < audio->queue_limit ?
               audio->queue_limit - (size_t)queued : 0;
         room &= ~(size_t)(AUDIO_FRAME_BYTES - 1);
         /* MI_AO keeps whole batches in normal blocking playback. FIFO
          * can accept a partial batch without changing the PCM stream. */
         if (audio->audioserver_mode || audio->nonblock)
            count = count < room ? count : room;
         else if (room < count)
            count = 0;
      } else if (audio->nonblock) {
         /* No safe space estimate. Never turn nonblocking into a wait. */
         break;
      }

      if (!count) {
         if (audio->nonblock)
            break;
         usleep(AUDIO_WAIT_US);
         continue;
      }

      if (audio->audioserver_mode) {
         if (audio->nonblock && !audio->pipe_nonblock)
            break;
         const ssize_t written = write(audio->audioserver_fd,
               (const uint8_t*)buf + total, count);
         if (written > 0)
            total += written;
         else if (written < 0 && errno == EINTR)
            continue;
         else if (written < 0 && errno == EAGAIN && !audio->nonblock) {
            usleep(AUDIO_WAIT_US);
            continue;
         } else
            break;
      } else {
         audio->AoSendFrame.apVirAddr[0] = (uint8_t*)buf + total;
         audio->AoSendFrame.u32Len = count;
         if (MI_AO_SendFrame(0, 0, &audio->AoSendFrame, 0) != 0)
            break;
         total += count;
      }
   }

   return total;
}

static ssize_t miao_write(void *data, const void *buf, size_t size)
{
#ifdef KORIKI_AUDIO_DIAG
   miao_audio_t *miaoaudio = (miao_audio_t*)data;
   int64_t t0 = diag_f ? diag_now_us() : 0;
#endif
   MP_BEGIN(mp_audio);
   ssize_t ret = miao_write_impl(data, buf, size);
   MP_END(MP_AUDIO, mp_audio);
#ifdef KORIKI_AUDIO_DIAG
   if (diag_f) {
      diag_blocked_us += diag_now_us() - t0;
      if (ret > 0) diag_bytes += ret;
      diag_writes++;
      diag_sample(miao_queued(miaoaudio));
      diag_flush(miaoaudio->audioserver_mode);
   }
#endif
   return ret;
}

static bool miao_stop(void *data)
{
   ((miao_audio_t*)data)->is_paused = true;
   return true;
}

static bool miao_start(void *data, bool is_shutdown)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;
   if (miaoaudio->is_paused) {
      if (!miaoaudio->audioserver_mode) {
         MI_AO_ClearChnBuf(0,0);
         miao_prime(miaoaudio);
      }
      miaoaudio->is_paused = false;
   }
   return true;
}

static bool miao_alive(void *data) { return !((miao_audio_t*)data)->is_paused; }

static void miao_set_nonblock_state(void *data, bool state)
{
   miao_audio_t *audio = (miao_audio_t*)data;
   if (audio->audioserver_mode) {
      int flags = fcntl(audio->audioserver_fd, F_GETFL);
      if (flags >= 0 && fcntl(audio->audioserver_fd, F_SETFL,
                             state ? flags | O_NONBLOCK : flags & ~O_NONBLOCK) == 0)
         audio->pipe_nonblock = state;
   }
   audio->nonblock = state;
}

static void miao_free(void *data)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;

   if (miaoaudio->audioserver_mode && miaoaudio->audioserver_fd >= 0)
      close(miaoaudio->audioserver_fd);
   else {
      MI_AO_ClearChnBuf(0,0);
      MI_AO_DisableChn(0,0);
      MI_AO_Disable(0);
   }

   free(miaoaudio->nullbuf);
   free(miaoaudio);
#ifdef KORIKI_AUDIO_DIAG
   if (diag_f) { fclose(diag_f); diag_f = NULL; }
#endif
}

static bool miao_use_float(void *data) { return false; }

static size_t miao_write_avail(void *data)
{
   miao_audio_t *audio = (miao_audio_t*)data;
   const long queued = miao_queued(audio);
   if (queued < 0)
      return audio->queue_limit / 2;  /* Neutral rate adjustment. */
   return (size_t)queued < audio->queue_limit ?
         audio->queue_limit - (size_t)queued : 0;
}

static size_t miao_buffer_size(void *data)
{
   return ((miao_audio_t*)data)->queue_limit;
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
