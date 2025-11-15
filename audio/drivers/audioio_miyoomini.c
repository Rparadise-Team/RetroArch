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
#include <pthread.h>

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

/* MI_AO_SendFrame Max bytes */
#define MIAO_MAX_BUFSIZE 51200

/* FIFO audioserver */
#define AUDIOSERVER_FIFO "/tmp/audio_fifo_server"

typedef struct miao_audio
{
   MI_AUDIO_Frame_t AoSendFrame;
   size_t bufsize;
   uint32_t freq;
   bool nonblock;
   bool is_paused;

   bool audioserver_mode;
   int audioserver_fd;

   void* nullbuf;

   bool need_volume_apply;
   int pending_volume;

   /* Async push (stock driver) */
   bool use_thread;
   bool thread_exit;
   bool thread_started;
   pthread_t worker;
   pthread_mutex_t lock;
   pthread_cond_t cond;
   uint8_t *ring_buf;
   size_t ring_size;
   size_t ring_write;
   size_t ring_read;
   size_t ring_pending;
} miao_audio_t;

#define MIAO_RING_MULTIPLIER 4

static bool miao_open_audioserver_fifo(miao_audio_t *ctx)
{
   if (!ctx)
      return false;

   if (ctx->audioserver_fd >= 0)
   {
      close(ctx->audioserver_fd);
      ctx->audioserver_fd = -1;
   }

   ctx->audioserver_fd = open(AUDIOSERVER_FIFO, O_WRONLY);
   if (ctx->audioserver_fd < 0)
   {
      RARCH_ERR("[MIAO]: Cannot open audioserver FIFO (errno=%d).\n", errno);
      return false;
   }

   return true;
}

static bool miao_fifo_write_blocking(miao_audio_t *ctx, const uint8_t *data, size_t size)
{
   size_t total = 0;

   if (!ctx || ctx->audioserver_fd < 0)
      return false;

   while (total < size)
   {
      ssize_t written = write(ctx->audioserver_fd, data + total, size - total);

      if (written > 0)
      {
         total += (size_t)written;
         continue;
      }

      if (written == 0)
         break;

      if (errno == EINTR)
         continue;

      if (errno == EAGAIN)
      {
         usleep(2000);
         continue;
      }

      if (errno == EPIPE || errno == ENXIO)
      {
         RARCH_WARN("[MIAO]: Audioserver FIFO closed, attempting to reopen.\n");
         if (miao_open_audioserver_fifo(ctx))
            continue;
         return false;
      }

      RARCH_ERR("[MIAO]: Audioserver FIFO write failed (errno=%d).\n", errno);
      return false;
   }

   return total == size;
}

static void *miao_worker_thread(void *userdata)
{
   miao_audio_t *ctx = (miao_audio_t*)userdata;

   while (true)
   {
      size_t to_write = 0;
      size_t start = 0;

      pthread_mutex_lock(&ctx->lock);
      while (!ctx->thread_exit && ctx->ring_pending == 0)
         pthread_cond_wait(&ctx->cond, &ctx->lock);

      if (ctx->thread_exit)
      {
         pthread_mutex_unlock(&ctx->lock);
         break;
      }

      to_write = (ctx->ring_pending > ctx->bufsize) ? ctx->bufsize : ctx->ring_pending;
      start    = ctx->ring_read;

      ctx->ring_read    = (ctx->ring_read + to_write) % ctx->ring_size;
      ctx->ring_pending -= to_write;

      pthread_cond_signal(&ctx->cond);
      pthread_mutex_unlock(&ctx->lock);

      while (to_write)
      {
         size_t tail = ctx->ring_size - start;
         size_t chunk = (to_write < tail) ? to_write : tail;
         uint8_t *chunk_ptr = ctx->ring_buf + start;

         if (ctx->audioserver_mode)
         {
            if (!miao_fifo_write_blocking(ctx, chunk_ptr, chunk))
            {
               RARCH_ERR("[MIAO]: Stopping worker thread after FIFO error.\n");
               goto worker_exit;
            }
         }
         else
         {
            ctx->AoSendFrame.apVirAddr[0] = chunk_ptr;
            ctx->AoSendFrame.u32Len       = chunk;
            MI_AO_SendFrame(0, 0, &ctx->AoSendFrame, 0);
         }

         to_write -= chunk;
         start     = (start + chunk) % ctx->ring_size;
      }
   }

worker_exit:
   return NULL;
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

   const int freqtable[] = { 8000,11025,12000,16000,22050,24000,32000,44100,48000 };
   for (uint32_t i=0; i<(sizeof(freqtable)/sizeof(int)); i++) {
      if (rate <= freqtable[i]) { miaoaudio->freq = freqtable[i]; break; }
   } 
   if (rate > 48000) miaoaudio->freq = 48000;

   if (miaoaudio->freq != rate) {
      *new_rate = miaoaudio->freq;
      RARCH_WARN("[MIAO]: Requested rate adjusted to %d Hz.\n", *new_rate);
   }

   miaoaudio->bufsize = (latency * miaoaudio->freq / 1000) << 2;
   miaoaudio->bufsize = (miaoaudio->bufsize + 15) & ~15;
   if ( miaoaudio->bufsize == 0 ) miaoaudio->bufsize = 16;
   else if ( miaoaudio->bufsize > MIAO_MAX_BUFSIZE ) miaoaudio->bufsize = MIAO_MAX_BUFSIZE;

   samples = miaoaudio->bufsize >> 2;
   if (samples > 2048) samples = 2048;

   miaoaudio->nullbuf = calloc(1, miaoaudio->bufsize);
   if (!miaoaudio->nullbuf) goto error;

   miaoaudio->AoSendFrame.apVirAddr[0] = miaoaudio->nullbuf;
   miaoaudio->AoSendFrame.u32Len = miaoaudio->bufsize;

   if (miaoaudio->audioserver_mode) {
      if (!miao_open_audioserver_fifo(miaoaudio))
         goto error;
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
      MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);
      RARCH_LOG("[MIAO]: Without audioserver\n");
   }

   miaoaudio->pending_volume   = getVolumeMM();
   miaoaudio->need_volume_apply = true;

   {
      size_t ring_size = miaoaudio->bufsize * MIAO_RING_MULTIPLIER;
      if (ring_size < miaoaudio->bufsize)
         ring_size = miaoaudio->bufsize;

      miaoaudio->ring_buf = (uint8_t*)malloc(ring_size);
      if (miaoaudio->ring_buf)
      {
         miaoaudio->ring_size = ring_size;
         if (pthread_mutex_init(&miaoaudio->lock, NULL) == 0)
         {
            if (pthread_cond_init(&miaoaudio->cond, NULL) == 0)
            {
               if (pthread_create(&miaoaudio->worker, NULL, miao_worker_thread, miaoaudio) == 0)
               {
                  miaoaudio->use_thread     = true;
                  miaoaudio->thread_started = true;
               }
               else
               {
                  pthread_mutex_destroy(&miaoaudio->lock);
                  pthread_cond_destroy(&miaoaudio->cond);
                  free(miaoaudio->ring_buf);
                  miaoaudio->ring_buf = NULL;
                  RARCH_WARN("[MIAO]: Unable to start worker thread, falling back to blocking writes.\n");
               }
            }
            else
            {
               pthread_mutex_destroy(&miaoaudio->lock);
               free(miaoaudio->ring_buf);
               miaoaudio->ring_buf = NULL;
               RARCH_WARN("[MIAO]: Failed to init worker condition variable, disabling async path.\n");
            }
         }
         else
         {
            RARCH_WARN("[MIAO]: Failed to init worker mutex, disabling async path.\n");
            free(miaoaudio->ring_buf);
            miaoaudio->ring_buf = NULL;
         }
      }
   }
   return miaoaudio;

error:
   if (miaoaudio->audioserver_fd >= 0) close(miaoaudio->audioserver_fd);
   free(miaoaudio->nullbuf);
   free(miaoaudio);
   return NULL;
}

static ssize_t miao_write(void *data, const void *buf, size_t size)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;
   if ((!size)||(miaoaudio->is_paused)) return 0;

   if (miaoaudio->need_volume_apply)
   {
      set_snd_level(miaoaudio->pending_volume);
      miaoaudio->need_volume_apply = false;
   }

   if (miaoaudio->audioserver_mode && (!miaoaudio->use_thread || !miaoaudio->ring_buf)) {
      if (!miao_fifo_write_blocking(miaoaudio, (const uint8_t*)buf, size))
         return 0;
      return (ssize_t)size;
   }

   if (miaoaudio->use_thread && miaoaudio->ring_buf)
   {
      size_t total = 0;
      const uint8_t *src = (const uint8_t*)buf;

      while (total < size)
      {
         size_t space;

         pthread_mutex_lock(&miaoaudio->lock);
         space = miaoaudio->ring_size - miaoaudio->ring_pending;
         while (space == 0 && !miaoaudio->nonblock && !miaoaudio->thread_exit)
         {
            pthread_cond_wait(&miaoaudio->cond, &miaoaudio->lock);
            space = miaoaudio->ring_size - miaoaudio->ring_pending;
         }

         if (space == 0 && miaoaudio->nonblock)
         {
            pthread_mutex_unlock(&miaoaudio->lock);
            break;
         }

         if (miaoaudio->thread_exit)
         {
            pthread_mutex_unlock(&miaoaudio->lock);
            break;
         }

         if (space > (size - total))
            space = size - total;

         if (!space)
         {
            pthread_mutex_unlock(&miaoaudio->lock);
            break;
         }

         size_t chunk = space;
         size_t tail  = miaoaudio->ring_size - miaoaudio->ring_write;
         size_t first = (chunk < tail) ? chunk : tail;

         memcpy(miaoaudio->ring_buf + miaoaudio->ring_write, src + total, first);
         if (chunk > first)
            memcpy(miaoaudio->ring_buf, src + total + first, chunk - first);

         miaoaudio->ring_write   = (miaoaudio->ring_write + chunk) % miaoaudio->ring_size;
         miaoaudio->ring_pending += chunk;
         pthread_cond_signal(&miaoaudio->cond);
         pthread_mutex_unlock(&miaoaudio->lock);

         total += chunk;
      }

      return (ssize_t)total;
   }

   miaoaudio->AoSendFrame.apVirAddr[0] = (void*)buf;
   ssize_t write_bytes;
   uint32_t usleepclock;
   MI_AO_ChnState_t status;

   MI_AO_QueryChnStat(0, 0, &status);
   int avail = miaoaudio->bufsize - status.u32ChnBusyNum;

   if ((avail < (int)size) && (!miaoaudio->nonblock)) {
      write_bytes = size;
      miaoaudio->AoSendFrame.u32Len = write_bytes;
      MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);

      MI_AO_QueryChnStat(0, 0, &status);
      if (status.u32ChnBusyNum > miaoaudio->bufsize) {
         usleepclock = (uint64_t)(status.u32ChnBusyNum - miaoaudio->bufsize) * 1000000 / (miaoaudio->freq << 2);
         if (usleepclock) usleep(usleepclock);
      }
   } else {
      write_bytes = (avail > (int)size) ? (int)size : avail;
      if (write_bytes > 0) {
         miaoaudio->AoSendFrame.u32Len = write_bytes;
         MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);
      } else return 0;
   }
   return write_bytes;
}

static bool miao_stop(void *data)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;
   miaoaudio->is_paused = true;

   if (miaoaudio->use_thread)
   {
      pthread_mutex_lock(&miaoaudio->lock);
      miaoaudio->ring_pending = 0;
      miaoaudio->ring_read = miaoaudio->ring_write;
      pthread_cond_signal(&miaoaudio->cond);
      pthread_mutex_unlock(&miaoaudio->lock);
   }
   return true;
}

static bool miao_start(void *data, bool is_shutdown)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;
   if (miaoaudio->is_paused) {
      if (!miaoaudio->audioserver_mode) {
         MI_AO_ClearChnBuf(0,0);
         MI_AO_SendFrame(0,0,&miaoaudio->AoSendFrame,0);
      }
      miaoaudio->is_paused = false;
   }
   return true;
}

static bool miao_alive(void *data) { return !((miao_audio_t*)data)->is_paused; }

static void miao_set_nonblock_state(void *data, bool state) { ((miao_audio_t*)data)->nonblock = state; }

static void miao_free(void *data)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;

   if (miaoaudio->use_thread && miaoaudio->thread_started)
   {
      pthread_mutex_lock(&miaoaudio->lock);
      miaoaudio->thread_exit = true;
      pthread_cond_signal(&miaoaudio->cond);
      pthread_mutex_unlock(&miaoaudio->lock);
      pthread_join(miaoaudio->worker, NULL);
      pthread_mutex_destroy(&miaoaudio->lock);
      pthread_cond_destroy(&miaoaudio->cond);
   }

   free(miaoaudio->ring_buf);

   if (miaoaudio->audioserver_mode && miaoaudio->audioserver_fd >= 0)
      close(miaoaudio->audioserver_fd);
   else {
      MI_AO_ClearChnBuf(0,0);
      MI_AO_DisableChn(0,0);
      MI_AO_Disable(0);
   }

   free(miaoaudio->nullbuf);
   free(miaoaudio);
}

static bool miao_use_float(void *data) { return false; }

static size_t miao_write_avail(void *data)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;
   size_t avail = MIAO_MAX_BUFSIZE;

   if (miaoaudio->use_thread && miaoaudio->ring_buf)
   {
      pthread_mutex_lock(&miaoaudio->lock);
      avail = miaoaudio->ring_size - miaoaudio->ring_pending;
      pthread_mutex_unlock(&miaoaudio->lock);
   }

   return avail;
}

static size_t miao_buffer_size(void *data)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;
   if (miaoaudio->use_thread && miaoaudio->ring_buf)
      return miaoaudio->ring_size;
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
