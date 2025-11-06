/* RetroArch - A frontend for libretro.
 * Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 * Copyright (C) 2011-2017 - Daniel De Matteis
 *
 * RetroArch is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Found-
 * ation, either version 3 of the License, or (at your option) any later version.
 *
 * RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with RetroArch.
 * If not, see <http://www.gnu.org/licenses/>.
 */

/*
MIAO audio driver for miyoomini
The name "audioio" is used to minimize the number of files to be rewritten as much as possible,
but /dev/audio is not used.
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <sys/select.h>
#define YIELD_WAIT /* Flag to wait with sched_yield() when the wait time is less than 10ms */
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
/* FIFO path for audioserver */
#define AUDIOSERVER_FIFO "/tmp/audio_fifo_server"

typedef struct miao_audio
{
   MI_AUDIO_Frame_t AoSendFrame;
   size_t bufsize;
   uint32_t freq;
   bool nonblock;
   bool is_paused;
   void* nullbuf;
   bool audioserver_mode;  /* Flag to indicate audioserver is running */
   int audioserver_fd;     /* File descriptor for audioserver FIFO */
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

   /* Use audiofix as the main condition for audioserver mode */
   int audiofix = getValueMM("audiofix");
   miaoaudio->audioserver_mode = (audiofix != 0);
   miaoaudio->audioserver_fd = -1;

   if (miaoaudio->audioserver_mode) {
      /* Open FIFO in write-only mode */
      miaoaudio->audioserver_fd = open(AUDIOSERVER_FIFO, O_WRONLY);
      if (miaoaudio->audioserver_fd < 0) {
         RARCH_ERR("[MIAO]: Failed to open audioserver FIFO (errno=%d)\n", errno);
         free(miaoaudio);
         return NULL;
      }
   }
	
   /* Set initial volume using set_snd_level */
   if (!miaoaudio->audioserver_mode) {
	   int target_vol = getVolumeMM();
       set_snd_level(target_vol);
   } else {
	  int volumeMM = setVolumeMM();
      char command[100];
      sprintf(command, "tinymix set 6 %d", volumeMM);
      system(command);
   }

   /* Continue with normal initialization */
   const int freqtable[] = { 8000,11025,12000,16000,22050,24000,32000,44100,48000 };
   for (uint32_t i=0; i<(sizeof(freqtable)/sizeof(int)); i++) {
      if (rate <= freqtable[i]) { miaoaudio->freq = freqtable[i]; break; }
   } if (rate > 48000) miaoaudio->freq = 48000;
   if (miaoaudio->freq != rate) {
      *new_rate = miaoaudio->freq;
      RARCH_WARN("[MIAO]: Requested sample rate not supported, adjusting output rate to %d Hz.\n", *new_rate);
   }

   miaoaudio->bufsize = (latency * miaoaudio->freq / 1000) << 2;
   miaoaudio->bufsize = (miaoaudio->bufsize + 15) & ~15;
   if ( miaoaudio->bufsize == 0 ) miaoaudio->bufsize = 16;
   else if ( miaoaudio->bufsize > MIAO_MAX_BUFSIZE ) miaoaudio->bufsize = MIAO_MAX_BUFSIZE;
   RARCH_LOG("[MIAO]: Requested %u ms latency, got %.2f ms\n",
            latency, (float)( (miaoaudio->bufsize >> 2) * 1000 / miaoaudio->freq ) );
   samples = miaoaudio->bufsize >> 2;
   if ( samples > 2048 ) samples = 2048;

   /* Only initialize MI_AO if not in audioserver mode */
   if (!miaoaudio->audioserver_mode) {
      memset(&attr, 0, sizeof(attr));
      attr.eSamplerate = (MI_AUDIO_SampleRate_e)miaoaudio->freq;
      attr.eSoundmode = E_MI_AUDIO_SOUND_MODE_STEREO;
      attr.u32ChnCnt = 2;
      attr.u32PtNumPerFrm = samples;
      miaoaudio->AoSendFrame.eSoundmode = E_MI_AUDIO_SOUND_MODE_STEREO;
      if (MI_AO_SetPubAttr(0,&attr)) goto error;
      if (MI_AO_Enable(0)) goto error;
      if (MI_AO_EnableChn(0,0)) goto error;
      if (MI_AO_SetMute(0,FALSE)) goto error;
      /* if (MI_AO_SetVolume(0,0)) goto error; */
      miaoaudio->nullbuf = calloc(1, miaoaudio->bufsize);
      if (!miaoaudio->nullbuf) goto error;
      miaoaudio->AoSendFrame.apVirAddr[0] = miaoaudio->nullbuf;
      miaoaudio->AoSendFrame.u32Len = miaoaudio->bufsize;
      MI_AO_ClearChnBuf(0,0);
      MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);
      RARCH_LOG("[MIAO]: Without audioserver\n");
   } else {
      /* In audioserver mode, we still need nullbuf for consistency */
      miaoaudio->nullbuf = calloc(1, miaoaudio->bufsize);
      if (!miaoaudio->nullbuf) goto error;
      miaoaudio->AoSendFrame.apVirAddr[0] = miaoaudio->nullbuf;
      miaoaudio->AoSendFrame.u32Len = miaoaudio->bufsize;
      RARCH_LOG("[MIAO]: With audioserver\n");
   }

   return miaoaudio;

error:
   if (miaoaudio->audioserver_fd >= 0) close(miaoaudio->audioserver_fd);
   free(miaoaudio->nullbuf);
   free(miaoaudio);
   RARCH_ERR("[MIAO]: Failed to initialize...\n");
   return NULL;
}

static ssize_t miao_write(void *data, const void *buf, size_t size)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;
   if ((!size)||(miaoaudio->is_paused)) return 0;

   /* If audioserver mode is active, write to FIFO */
   if (miaoaudio->audioserver_mode && miaoaudio->audioserver_fd >= 0) {
      const uint8_t *write_buf = (const uint8_t*)buf;
      size_t total_written = 0;
      
      /* Write in chunks, handle blocking intelligently */
      while (total_written < size) {
         ssize_t ret = write(miaoaudio->audioserver_fd, write_buf + total_written, size - total_written);
         
         if (ret > 0) {
            total_written += ret;
         } else if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
               /* FIFO full, wait a bit if blocking mode */
               if (miaoaudio->nonblock) {
                  /* In nonblock mode, return what we wrote */
                  return (total_written > 0) ? total_written : 0;
               }
               /* Wait for FIFO to have space using select */
               fd_set wfds;
               struct timeval tv;
               FD_ZERO(&wfds);
               FD_SET(miaoaudio->audioserver_fd, &wfds);
               tv.tv_sec = 0;
               tv.tv_usec = 10000; /* 10ms timeout */
               select(miaoaudio->audioserver_fd + 1, NULL, &wfds, NULL, &tv);
               continue;
            } else if (errno == EINTR) {
               /* Interrupted, retry */
               continue;
            } else {
               /* Real error */
               RARCH_ERR("[MIAO]: audioserver FIFO write error (errno=%d)\n", errno);
               return -1;
            }
         } else {
            /* ret == 0, shouldn't happen with FIFO but handle it */
            break;
         }
      }
      return total_written;
   }

   /* Normal mode operation (same as original) */
   miaoaudio->AoSendFrame.apVirAddr[0] = (void*)buf;
   ssize_t write_bytes;
   uint32_t usleepclock;
   MI_AO_ChnState_t status;
   MI_AO_QueryChnStat(0, 0, &status);
   int avail = miaoaudio->bufsize - status.u32ChnBusyNum;
   if ( (avail < size) && (!miaoaudio->nonblock) ) {
      write_bytes = size;
      miaoaudio->AoSendFrame.u32Len = write_bytes;
      MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);
      MI_AO_QueryChnStat(0, 0, &status);
      if (status.u32ChnBusyNum > miaoaudio->bufsize) {
         usleepclock = (uint64_t)(status.u32ChnBusyNum - miaoaudio->bufsize) * 1000000 / (miaoaudio->freq << 2);
#ifndef YIELD_WAIT
         if ( usleepclock ) usleep(usleepclock);
#else
         if ( usleepclock > 0x2800 ) usleep(usleepclock - 0x2800); /* 0.24ms margin */
         const struct sched_param scprm = {0};
         int policy = sched_getscheduler(0);
         sched_setscheduler(0, SCHED_IDLE, &scprm);
         do { sched_yield(); MI_AO_QueryChnStat(0, 0, &status);
         } while(status.u32ChnBusyNum > miaoaudio->bufsize);
         sched_setscheduler(0, policy, &scprm);
#endif
      }
   } else {
      write_bytes = avail > size ? size : avail;
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
   if (miaoaudio->is_paused) {
      return true;
   }
   RARCH_LOG("[MIAO audio]: Pausing.\n");
   if (!miaoaudio->is_paused) {
      miaoaudio->is_paused = true;
   }
   return true;
}

static bool miao_start(void *data, bool is_shutdown)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;
   if (!miaoaudio) return false;
   if (is_shutdown)
      return true;
   if (miaoaudio->is_paused) {
      RARCH_LOG("[MIAO audio]: Resuming.\n");
      /* Only send pre-fill data in normal mode */
      if (!miaoaudio->audioserver_mode) {
         miaoaudio->AoSendFrame.apVirAddr[0] = miaoaudio->nullbuf;
         miaoaudio->AoSendFrame.u32Len = miaoaudio->bufsize;
         MI_AO_ClearChnBuf(0,0);
         MI_AO_SendFrame(0, 0, &miaoaudio->AoSendFrame, 0);
      }
      miaoaudio->is_paused = false;
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
   if (!miaoaudio) return;
   
   miaoaudio->nonblock = state;
   
   /* If audioserver mode, also set nonblock flag on the fd if needed */
   if (miaoaudio->audioserver_mode && miaoaudio->audioserver_fd >= 0) {
      int flags = fcntl(miaoaudio->audioserver_fd, F_GETFL);
      if (state)
         fcntl(miaoaudio->audioserver_fd, F_SETFL, flags | O_NONBLOCK);
      else
         fcntl(miaoaudio->audioserver_fd, F_SETFL, flags & (~O_NONBLOCK));
   }
}

static void miao_free(void *data)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;
   
   /* Close audioserver fd if open */
   if (miaoaudio->audioserver_fd >= 0) {
      close(miaoaudio->audioserver_fd);
   }
   
   /* Only cleanup MI_AO if not in audioserver mode */
   if (!miaoaudio->audioserver_mode) {
      MI_AO_ClearChnBuf(0,0);
      MI_AO_DisableChn(0,0);
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
   /* In audioserver mode, assume buffer space is available */
   if (miaoaudio->audioserver_mode && miaoaudio->audioserver_fd >= 0) {
      return MIAO_MAX_BUFSIZE;
   }
   /* Normal mode operation */
   MI_AO_ChnState_t status;
   MI_AO_QueryChnStat(0, 0, &status);
   int avail = MIAO_MAX_BUFSIZE - status.u32ChnBusyNum;
   return (avail > 0) ? avail : 0;
}

static size_t miao_buffer_size(void *data)
{
   miao_audio_t *miaoaudio = (miao_audio_t*)data;
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
