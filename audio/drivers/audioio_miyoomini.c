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
} miao_audio_t;

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
      miaoaudio->audioserver_fd = open(AUDIOSERVER_FIFO, O_WRONLY);
      if (miaoaudio->audioserver_fd < 0) {
         RARCH_ERR("[MIAO]: Cannot open audioserver FIFO.\n");
         goto error;
      }
      RARCH_LOG("[MIAO]: Whit audioserver\n");
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
      RARCH_LOG("[MIAO]: Whitout audioserver\n");
   }

   int target_vol = getVolumeMM();
   set_snd_level(target_vol);
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

   if (miaoaudio->audioserver_mode && miaoaudio->audioserver_fd >= 0) {
      size_t total = 0;
      while (total < size) {
         ssize_t w = write(miaoaudio->audioserver_fd, (uint8_t*)buf + total, size - total);
         if (w > 0) total += w;
         else if (w < 0 && errno == EAGAIN) {
            usleep(2000);
            continue;
         } else break;
      }
      return total;
   }

   miaoaudio->AoSendFrame.apVirAddr[0] = (void*)buf;
   ssize_t write_bytes;
   uint32_t usleepclock;
   MI_AO_ChnState_t status;

   MI_AO_QueryChnStat(0, 0, &status);
   int avail = miaoaudio->bufsize - status.u32ChnBusyNum;

   if ((avail < size) && (!miaoaudio->nonblock)) {
      write_bytes = size;
      miaoaudio->AoSendFrame.u32Len = write_bytes;
      MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);

      MI_AO_QueryChnStat(0, 0, &status);
      if (status.u32ChnBusyNum > miaoaudio->bufsize) {
         usleepclock = (uint64_t)(status.u32ChnBusyNum - miaoaudio->bufsize) * 1000000 / (miaoaudio->freq << 2);
         if (usleepclock) usleep(usleepclock);
      }
   } else {
      write_bytes = (avail > size) ? size : avail;
      if (write_bytes > 0) {
         miaoaudio->AoSendFrame.u32Len = write_bytes;
         MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);
      } else return 0;
   }
   return write_bytes;
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

static size_t miao_write_avail(void *data) { return MIAO_MAX_BUFSIZE; }

static size_t miao_buffer_size(void *data) { return MIAO_MAX_BUFSIZE; }

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
