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
#include "miyoomini_audio_common.h"

#define MIAO_MAX_BUFSIZE 51200
#define AUDIOSERVER_FIFO "/tmp/audio_fifo_server"

typedef struct miao_audio
{
   MI_AUDIO_Frame_t frame;
   size_t bufsize;
   uint32_t freq;
   bool nonblock;
   bool is_paused;

   bool audioserver_mode;
   int audioserver_fd;
   size_t fifo_chunk;
   miyoo_audio_timing_t timing;

   void *nullbuf;
} miao_audio_t;

static bool miao_hw_enable(uint32_t freq, uint32_t samples)
{
   MI_AUDIO_Attr_t attr;

   memset(&attr, 0, sizeof(attr));
   attr.eSamplerate    = (MI_AUDIO_SampleRate_e)freq;
   attr.eSoundmode     = E_MI_AUDIO_SOUND_MODE_STEREO;
   attr.u32ChnCnt      = 2;
   attr.u32PtNumPerFrm = samples;

   if (MI_AO_SetPubAttr(0, &attr) != MI_SUCCESS)
      return false;
   if (MI_AO_Enable(0) != MI_SUCCESS)
      return false;
   if (MI_AO_EnableChn(0, 0) != MI_SUCCESS)
      return false;
   if (MI_AO_SetMute(0, FALSE) != MI_SUCCESS)
      return false;

   MI_AO_ClearChnBuf(0, 0);
   return true;
}

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

static bool miao_init_hw_context(miao_audio_t *ctx, uint32_t samples)
{
   if (!ctx)
      return false;

   if (!miao_hw_enable(ctx->freq, samples))
      return false;

   ctx->frame.eSoundmode   = E_MI_AUDIO_SOUND_MODE_STEREO;
   ctx->frame.apVirAddr[0] = ctx->nullbuf;
   ctx->frame.u32Len       = ctx->bufsize;

   MI_AO_SendFrame(0, 0, &ctx->frame, 0);
   RARCH_LOG("[MIAO]: direct MI_AO path.\n");
   return true;
}

static void miao_dispose_hw(void)
{
   MI_AO_ClearChnBuf(0, 0);
   MI_AO_DisableChn(0, 0);
   MI_AO_Disable(0);
}

static void *miao_init(const char *device,
      unsigned rate, unsigned latency,
      unsigned block_frames,
      unsigned *new_rate)
{
   uint32_t samples;
   miao_audio_t *ctx = (miao_audio_t*)calloc(1, sizeof(*ctx));
   int audiofix;

   (void)device;
   (void)block_frames;

   if (!ctx)
      return NULL;

   ctx->freq = miyoo_audio_select_rate(rate);
   if (ctx->freq != rate)
   {
      *new_rate = ctx->freq;
      RARCH_WARN("[MIAO]: Requested rate adjusted to %u Hz.\n", ctx->freq);
   }

   ctx->bufsize = (latency * ctx->freq / 1000u) << 2;
   ctx->bufsize = (ctx->bufsize + 15u) & ~15u;
   if (!ctx->bufsize)
      ctx->bufsize = 16u;
   else if (ctx->bufsize > MIAO_MAX_BUFSIZE)
      ctx->bufsize = MIAO_MAX_BUFSIZE;

   samples = ctx->bufsize >> 2;
   if (samples > 2048u)
      samples = 2048u;

   ctx->nullbuf = calloc(1, ctx->bufsize);
   if (!ctx->nullbuf)
      goto error;

   miyoo_audio_timing_init(&ctx->timing, ctx->freq, ctx->bufsize);

   audiofix = getValueMM("audiofix");
   ctx->audioserver_mode = (audiofix != 0);
   ctx->audioserver_fd   = -1;

   if (ctx->audioserver_mode)
   {
      if (!miao_open_audioserver(ctx))
         goto error;
   }
   else if (!miao_init_hw_context(ctx, samples))
      goto error;

   ctx->frame.apVirAddr[0] = ctx->nullbuf;
   ctx->frame.u32Len       = ctx->bufsize;

   int target_vol = getVolumeMM();
   set_snd_level(target_vol);

   ctx->is_paused = false;
   ctx->nonblock  = false;

   return ctx;

error:
   if (ctx->audioserver_fd >= 0)
      close(ctx->audioserver_fd);
   free(ctx->nullbuf);
   free(ctx);
   return NULL;
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

   if (!ctx || ctx->audioserver_fd < 0)
      return -1;

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

      if (wrote < 0 && (errno == EINTR))
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

static ssize_t miao_write_hw(miao_audio_t *ctx, const uint8_t *data, size_t size)
{
   size_t total = 0;

   if (!ctx)
      return -1;

   while (total < size)
   {
      MI_AO_ChnState_t status;
      size_t avail;
      size_t chunk;
      useconds_t backoff;

      if (MI_AO_QueryChnStat(0, 0, &status) != MI_SUCCESS)
         break;

      if (status.u32ChnBusyNum >= ctx->bufsize)
         avail = 0;
      else
         avail = ctx->bufsize - status.u32ChnBusyNum;

      if (!avail)
      {
         if (ctx->nonblock)
            break;
         backoff = miyoo_audio_backpressure(&ctx->timing, status.u32ChnBusyNum);
         if (backoff)
            usleep(backoff);
         continue;
      }

      chunk = size - total;
      if (chunk > avail)
         chunk = avail;
      if (chunk > ctx->bufsize)
         chunk = ctx->bufsize;

      ctx->frame.apVirAddr[0] = (void*)(data + total);
      ctx->frame.u32Len       = (MI_U32)chunk;

      if (MI_AO_SendFrame(0, 0, &ctx->frame, 0) == MI_SUCCESS)
      {
         total += chunk;
         backoff = miyoo_audio_backpressure(&ctx->timing,
               status.u32ChnBusyNum + (uint32_t)chunk);
         if (backoff)
            usleep(backoff);
         continue;
      }

      if (ctx->nonblock)
         break;

      backoff = miyoo_audio_backpressure(&ctx->timing, ctx->bufsize);
      if (!backoff)
         backoff = 500;
      usleep(backoff);
   }

   return (ssize_t)total;
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

   ctx->is_paused = true;
   return true;
}

static bool miao_start(void *data, bool is_shutdown)
{
   miao_audio_t *ctx = (miao_audio_t*)data;

   (void)is_shutdown;

   if (!ctx)
      return false;

   if (ctx->is_paused)
   {
      if (!ctx->audioserver_mode)
      {
         MI_AO_ClearChnBuf(0, 0);
         MI_AO_SendFrame(0, 0, &ctx->frame, 0);
      }
      ctx->is_paused = false;
   }

   return true;
}

static bool miao_alive(void *data)
{
   return data && !((miao_audio_t*)data)->is_paused;
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
      miao_dispose_hw();

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
   (void)data;
   return MIAO_MAX_BUFSIZE;
}

static size_t miao_buffer_size(void *data)
{
   (void)data;
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
