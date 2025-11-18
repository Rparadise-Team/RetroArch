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

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define YIELD_WAIT /* Flag to wait with sched_yield() when the wait time is less than 10ms */

#include <mi_ao.h>
#ifdef  YIELD_WAIT
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
#include "miyoomini_audio_common.h"

/* MI_AO_SendFrame Max bytes */
#define MIAO_MAX_BUFSIZE 51200
#define AUDIOSERVER_FIFO "/tmp/audio_fifo_server"

typedef struct miao_audio
{
   MI_AUDIO_Frame_t AoSendFrame;
   size_t bufsize;
   uint32_t freq;
   bool nonblock;
   bool is_paused;
   void *nullbuf;
   bool audioserver_mode;
   int audioserver_fd;
   size_t fifo_chunk;
   miyoo_audio_timing_t timing;
} miao_audio_t;

static bool miao_open_audioserver(miao_audio_t *ctx)
{
   if (!ctx)
      return false;

   ctx->audioserver_fd = open(AUDIOSERVER_FIFO, O_WRONLY | O_CLOEXEC);
   if (ctx->audioserver_fd < 0)
   {
      RARCH_ERR("[MIAO]: Cannot open audioserver FIFO (errno=%d).\n", errno);
      return false;
   }

   ctx->fifo_chunk = miyoo_audio_fifo_chunk(&ctx->timing);
   RARCH_LOG("[MIAO]: using audioserver FIFO.\n");
   return true;
}

static bool miao_try_reopen_fifo(miao_audio_t *ctx)
{
   if (!ctx)
      return false;

   if (ctx->audioserver_fd >= 0)
      close(ctx->audioserver_fd);

   ctx->audioserver_fd = open(AUDIOSERVER_FIFO, O_WRONLY | O_CLOEXEC);
   if (ctx->audioserver_fd < 0)
   {
     RARCH_ERR("[MIAO]: Unable to reopen audioserver FIFO (errno=%d).\n", errno);
     return false;
   }

   RARCH_WARN("[MIAO]: Audioserver FIFO reconnected.\n");
   return true;
}

static ssize_t miao_write_fifo(miao_audio_t *ctx, const uint8_t *data, size_t size)
{
   size_t total = 0;

   if (!ctx || !data || !size || ctx->audioserver_fd < 0)
      return 0;

   while (total < size)
   {
      size_t to_write = size - total;
      if (to_write > ctx->fifo_chunk)
         to_write = ctx->fifo_chunk;

      ssize_t wrote = write(ctx->audioserver_fd, data + total, to_write);
      if (wrote > 0)
      {
         total += (size_t)wrote;
         continue;
      }

      if (wrote < 0 && errno == EINTR)
         continue;
      if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      {
         usleep(1000);
         continue;
      }

      if (wrote < 0 && (errno == EPIPE || errno == ENXIO))
      {
         if (miao_try_reopen_fifo(ctx))
            continue;
      }

      RARCH_ERR("[MIAO]: FIFO write failed (errno=%d).\n", errno);
      break;
   }

   return (ssize_t)total;
}

static void miao_hw_shutdown(void)
{
   MI_AO_ClearChnBuf(0, 0);
   MI_AO_DisableChn(0, 0);
   MI_AO_Disable(0);
}

static bool miao_hw_init(miao_audio_t *ctx, uint32_t samples)
{
   MI_AUDIO_Attr_t attr;

   if (!ctx)
      return false;

   memset(&attr, 0, sizeof(attr));
   attr.eSamplerate    = (MI_AUDIO_SampleRate_e)ctx->freq;
   attr.eSoundmode     = E_MI_AUDIO_SOUND_MODE_STEREO;
   attr.u32ChnCnt      = 2;
   attr.u32PtNumPerFrm = samples;
   ctx->AoSendFrame.eSoundmode = E_MI_AUDIO_SOUND_MODE_STEREO;

   if (MI_AO_SetPubAttr(0, &attr))
      return false;
   if (MI_AO_Enable(0))
      return false;
   if (MI_AO_EnableChn(0, 0))
      return false;
   if (MI_AO_SetMute(0, FALSE))
      return false;

   ctx->AoSendFrame.apVirAddr[0] = ctx->nullbuf;
   ctx->AoSendFrame.u32Len       = ctx->bufsize;
   MI_AO_ClearChnBuf(0, 0);
   MI_AO_SendFrame(0, 0, &ctx->AoSendFrame, 0);
   RARCH_LOG("[MIAO]: direct MI_AO path.\n");
   return true;
}

static void *miao_init(const char *device,
      unsigned rate, unsigned latency,
      unsigned block_frames,
      unsigned *new_rate)
{
   uint32_t samples;
   int audiofix;
   miao_audio_t *ctx = (miao_audio_t*)calloc(1, sizeof(*ctx));

   (void)device;
   (void)block_frames;

   if (!ctx)
      return NULL;

   ctx->audioserver_fd   = -1;
   ctx->fifo_chunk       = 0;
   ctx->audioserver_mode = false;

   const int freqtable[] = { 8000,11025,12000,16000,22050,24000,32000,44100,48000 };
   for (uint32_t i = 0; i < (sizeof(freqtable) / sizeof(int)); i++)
   {
      if (rate <= (unsigned)freqtable[i])
      {
         ctx->freq = (uint32_t)freqtable[i];
         break;
      }
   }
   if (rate > 48000)
      ctx->freq = 48000;

   if (ctx->freq != rate)
   {
      *new_rate = ctx->freq;
      RARCH_WARN("[MIAO]: Requested sample rate not supported, adjusting output rate to %u Hz.\n", ctx->freq);
   }

   ctx->bufsize = (latency * ctx->freq / 1000u) << 2;
   ctx->bufsize = (ctx->bufsize + 15u) & ~15u;
   if (!ctx->bufsize)
      ctx->bufsize = 16u;
   else if (ctx->bufsize > MIAO_MAX_BUFSIZE)
      ctx->bufsize = MIAO_MAX_BUFSIZE;

   RARCH_LOG("[MIAO]: Requested %u ms latency, got %.2f ms\n",
         latency, (float)((ctx->bufsize >> 2) * 1000u / ctx->freq));

   samples = ctx->bufsize >> 2;
   if (samples > 2048u)
      samples = 2048u;

   ctx->nullbuf = calloc(1, ctx->bufsize);
   if (!ctx->nullbuf)
      goto error;

   miyoo_audio_timing_init(&ctx->timing, ctx->freq, ctx->bufsize);
   audiofix = getValueMM("audiofix");
   ctx->audioserver_mode = (audiofix != 0);

   if (ctx->audioserver_mode)
   {
      if (!miao_open_audioserver(ctx))
         goto error;
   }
   else if (!miao_hw_init(ctx, samples))
      goto error;

   set_snd_level(getVolumeMM());

   ctx->is_paused = false;
   ctx->nonblock  = false;

   return ctx;

error:
   if (ctx->audioserver_fd >= 0)
      close(ctx->audioserver_fd);
   free(ctx->nullbuf);
   free(ctx);
   RARCH_ERR("[MIAO]: Failed to initialize...\n");
   return NULL;
}

static ssize_t miao_write_hw(miao_audio_t *ctx, const uint8_t *buf, size_t size)
{
   ssize_t write_bytes;
   uint32_t usleepclock;
   MI_AO_ChnState_t status;
   int avail;

   if (!ctx || !buf || !size || ctx->is_paused)
      return 0;

   ctx->AoSendFrame.apVirAddr[0] = (void*)buf;
   MI_AO_QueryChnStat(0, 0, &status);
   avail = (int)ctx->bufsize - (int)status.u32ChnBusyNum;

   if ((avail < (int)size) && (!ctx->nonblock))
   {
      write_bytes = (ssize_t)size;
      ctx->AoSendFrame.u32Len = (MI_U32)write_bytes;
      MI_AO_SendFrame(0, 0, &ctx->AoSendFrame, 0);

      MI_AO_QueryChnStat(0, 0, &status);
      if (status.u32ChnBusyNum > ctx->bufsize)
      {
         usleepclock = (uint32_t)(((uint64_t)(status.u32ChnBusyNum - ctx->bufsize) * 1000000ULL) / (ctx->freq << 2));
#ifndef YIELD_WAIT
         if (usleepclock)
            usleep(usleepclock);
#else
         if (usleepclock > 0x2800u)
            usleep(usleepclock - 0x2800u);

         {
            const struct sched_param scprm = {0};
            int policy = sched_getscheduler(0);
            sched_setscheduler(0, SCHED_IDLE, &scprm);
            do
            {
               sched_yield();
               MI_AO_QueryChnStat(0, 0, &status);
            } while(status.u32ChnBusyNum > ctx->bufsize);
            sched_setscheduler(0, policy, &scprm);
         }
#endif
      }
   }
   else
   {
      write_bytes = avail > (int)size ? (ssize_t)size : (ssize_t)avail;
      if (write_bytes > 0)
      {
         ctx->AoSendFrame.u32Len = (MI_U32)write_bytes;
         MI_AO_SendFrame(0, 0, &ctx->AoSendFrame, 0);
      }
      else
         return 0;
   }

   return write_bytes;
}

static ssize_t miao_write(void *data, const void *buf, size_t size)
{
   miao_audio_t *ctx = (miao_audio_t*)data;

   if (!ctx || !buf || !size || ctx->is_paused)
      return 0;

   if (ctx->audioserver_mode)
      return miao_write_fifo(ctx, (const uint8_t*)buf, size);

   return miao_write_hw(ctx, (const uint8_t*)buf, size);
}

static bool miao_stop(void *data)
{
   miao_audio_t *ctx = (miao_audio_t*)data;
   if (!ctx)
      return false;

   if (!ctx->is_paused)
      RARCH_LOG("[MIAO audio]: Pausing.\n");

   ctx->is_paused = true;
   return true;
}

static bool miao_start(void *data, bool is_shutdown)
{
   miao_audio_t *ctx = (miao_audio_t*)data;
   if (!ctx)
      return false;

   if (is_shutdown)
      return true;

   if (ctx->is_paused)
   {
      if (!ctx->audioserver_mode)
      {
         ctx->AoSendFrame.apVirAddr[0] = ctx->nullbuf;
         ctx->AoSendFrame.u32Len       = ctx->bufsize;
         MI_AO_ClearChnBuf(0, 0);
         MI_AO_SendFrame(0, 0, &ctx->AoSendFrame, 0);
      }
      ctx->is_paused = false;
   }

   return true;
}

static bool miao_alive(void *data)
{
   miao_audio_t *ctx = (miao_audio_t*)data;

   if (!ctx)
      return false;
   return !ctx->is_paused;
}

static void miao_set_nonblock_state(void *data, bool state)
{
   miao_audio_t *ctx = (miao_audio_t*)data;
   if (!ctx)
      return;
   ctx->nonblock = state;
}

static void miao_free(void *data)
{
   miao_audio_t *ctx = (miao_audio_t*)data;
   if (!ctx)
      return;

   if (ctx->audioserver_mode)
   {
      if (ctx->audioserver_fd >= 0)
         close(ctx->audioserver_fd);
   }
   else
      miao_hw_shutdown();

   free(ctx->nullbuf);
   free(ctx);
}

static bool miao_use_float(void *data)
{
   (void)data;
   return false;
}

static size_t miao_write_avail(void *data)
{
   miao_audio_t *ctx = (miao_audio_t*)data;
   MI_AO_ChnState_t status;

   if (!ctx)
      return 0;

   if (ctx->audioserver_mode)
      return ctx->fifo_chunk ? ctx->fifo_chunk : MIAO_MAX_BUFSIZE;

   if (MI_AO_QueryChnStat(0, 0, &status) != MI_SUCCESS)
      return 0;

   {
      int avail = (int)ctx->bufsize - (int)status.u32ChnBusyNum;
      return (avail > 0) ? (size_t)avail : 0u;
   }
}

static size_t miao_buffer_size(void *data)
{
   miao_audio_t *ctx = (miao_audio_t*)data;
   if (!ctx)
      return MIAO_MAX_BUFSIZE;

   if (ctx->audioserver_mode)
      return ctx->fifo_chunk ? ctx->fifo_chunk : MIAO_MAX_BUFSIZE;

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
