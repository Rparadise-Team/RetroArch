
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
      Modified OSS driver exclusively for miyoomini
      Supports both AudioFix: ON / OFF
*/

/* To use audioserver, must use open instead of open64 */
#ifdef _FILE_OFFSET_BITS
#undef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 32
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <sys/soundcard.h>
#include <mi_ao.h>
#include "volume/volume.h"

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

#include "../audio_driver.h"
#include "../../verbosity.h"
#include "miyoo_audioserver_fifo.h"

#define DEFAULT_OSS_DEV "/dev/dsp"

typedef struct oss_audio
{
   int fd;
   bool is_paused;
   bool nonblock;
   bool audioserver;
   size_t fifo_limit;
   size_t capacity;
   bool fifo_nonblock;
} oss_audio_t;

static bool oss_is_audioserver_fifo(int fd)
{
   struct stat opened, server;
   return fstat(fd, &opened) == 0 && S_ISFIFO(opened.st_mode) &&
          stat(MIYOO_AUDIOSERVER_FIFO, &server) == 0 &&
          opened.st_dev == server.st_dev && opened.st_ino == server.st_ino;
}

static void *oss_init(const char *device,
      unsigned rate, unsigned latency,
      unsigned block_frames,
      unsigned *new_out_rate)
{
   int frags, frag, channels, format, new_rate;
   oss_audio_t *ossaudio  = (oss_audio_t*)calloc(1, sizeof(oss_audio_t));
   const char *oss_device = device ? device : DEFAULT_OSS_DEV;

   if (!ossaudio)
      return NULL;

   /* Open /dev/dsp with audioserver check */
   /* Use the fact that padsp replaces open, but not __open */
   extern int __open(const char *file, int oflag);
   if ((ossaudio->fd = __open(oss_device, O_WRONLY)) < 0) {
      if ((ossaudio->fd = open(oss_device, O_WRONLY)) < 0) {
         free(ossaudio);
         perror("open");
         return NULL;
      }
      ossaudio->audioserver = true;
      new_rate = rate;
      RARCH_LOG("[OSS]: with audioserver.\n");
   } else {
      /* stock oss supports 48k, 32k, 16k, 8k only */
      if ( rate > 32000 ) new_rate = 48000;
      else if ( rate > 16000 ) new_rate = 32000;
      else if ( rate > 8000 ) new_rate = 16000;
      else new_rate = 8000;
	  RARCH_LOG("[OSS]: whitout audioserver.\n");
   }

   frags = (latency * new_rate * 4) / (1000 * (1 << 10));
#ifdef NO_MMP
   frag  = (frags << 16) | 10; /* 1024 bytes - más estable en SSD20X */
#else
   frag  = (frags << 16) | 9; /* 512 bytes - latencia baja en SSD202 */
#endif

   if (ioctl(ossaudio->fd, SNDCTL_DSP_SETFRAGMENT, &frag) < 0)
      RARCH_WARN("Cannot set fragment sizes. Latency might not be as expected ...\n");

   channels = 2;
   format   = AFMT_S16_LE;

   if (ioctl(ossaudio->fd, SNDCTL_DSP_CHANNELS, &channels) < 0)
      goto error;

   if (ioctl(ossaudio->fd, SNDCTL_DSP_SETFMT, &format) < 0)
      goto error;

   if (ioctl(ossaudio->fd, SNDCTL_DSP_SPEED, &new_rate) < 0)
      goto error;

   if (new_rate != (int)rate)
   {
      RARCH_WARN("Requested sample rate not supported. Adjusting output rate to %d Hz.\n", new_rate);
      *new_out_rate = new_rate;
   }

   if (!ossaudio->audioserver) {
      /* mi_ao init is required for stock oss */
      MI_AUDIO_Attr_t attr;
      memset(&attr, 0, sizeof(attr));
      attr.eSamplerate = new_rate;
      attr.eSoundmode = E_MI_AUDIO_SOUND_MODE_STEREO;
      attr.u32ChnCnt = 2;
      attr.u32PtNumPerFrm = 256;
      MI_AO_SetPubAttr(0, &attr);
   }
	
   if (ossaudio->audioserver && oss_is_audioserver_fifo(ossaudio->fd))
      ossaudio->fifo_limit = miyoo_fifo_configure(ossaudio->fd, new_rate, latency);
   else
   {
      audio_buf_info info = {0};
      ossaudio->capacity = 2; /* A neutral, nonzero point if GETOSPACE is unavailable. */
      if (ioctl(ossaudio->fd, SNDCTL_DSP_GETOSPACE, &info) >= 0 &&
          info.fragsize > 0 && info.fragstotal > 0 &&
          (uint64_t)info.fragsize * info.fragstotal <= INT32_MAX)
      {
         const size_t capacity = (size_t)info.fragsize * info.fragstotal;
         if (capacity >= 2)
            ossaudio->capacity = capacity;
      }
      if (ossaudio->audioserver)
         RARCH_WARN("[OSS]: audioserver descriptor is not the Koriki FIFO; using OSS space.\n");
      if (ossaudio->capacity == 2)
         RARCH_WARN("[OSS]: No valid OSS buffer size; using neutral rate control.\n");
   }

   int target_vol = getVolumeMM();
   set_snd_level(target_vol);

   return ossaudio;

error:
   close(ossaudio->fd);
   if (ossaudio)
      free(ossaudio);
   perror("ioctl");
   return NULL;
}

static ssize_t oss_write(void *data, const void *buf, size_t size)
{
   ssize_t ret, total_written = 0;
   oss_audio_t *ossaudio = (oss_audio_t*)data;
   if (ossaudio->fifo_limit)
   {
      if (ossaudio->nonblock && !ossaudio->fifo_nonblock)
         return 0;
      return miyoo_fifo_write(ossaudio->fd, ossaudio->fifo_limit,
            ossaudio->nonblock, buf, size);
   }

   /* For stock oss, no playback during fast forward to avoid blocking */
   if ( (size == 0) || ((!ossaudio->audioserver)&&(ossaudio->nonblock)) )
      return 0;
	
// OPTIMIZACIÓN: Escribe en bloques pequeños para evitar bloqueos
#ifdef NO_MMP
   size_t chunk_size = 1024;  // Escribir en chunks de 2048 bytes
#else
   size_t chunk_size = 512;  // Escribir en chunks de 512 bytes
#endif
   const uint8_t *src = (const uint8_t *)buf;
   
   while (total_written < size)
   {
      size_t to_write = (size - total_written) > chunk_size ? 
                        chunk_size : (size - total_written);
      
      if ((ret = write(ossaudio->fd, src + total_written, to_write)) < 0)
      {
         if (errno == EAGAIN && (fcntl(ossaudio->fd, F_GETFL) & O_NONBLOCK))
            return total_written;  // Retorna lo que escribió
         return -1;
      }
      total_written += ret;
   }

   return total_written;
}

static bool oss_stop(void *data)
{
   oss_audio_t *ossaudio  = (oss_audio_t*)data;

   ossaudio->is_paused = true;
   return true;
}

static bool oss_start(void *data, bool is_shutdown)
{
   oss_audio_t *ossaudio  = (oss_audio_t*)data;
   if (!ossaudio) return false;

   ossaudio->is_paused = false;
   return true;
}

static bool oss_alive(void *data)
{
   oss_audio_t *ossaudio  = (oss_audio_t*)data;
   return !ossaudio->is_paused;
}

static void oss_set_nonblock_state(void *data, bool state)
{
   oss_audio_t *ossaudio  = (oss_audio_t*)data;

   if (ossaudio->fifo_limit)
   {
      if (miyoo_fifo_set_nonblock(ossaudio->fd, state))
         ossaudio->fifo_nonblock = state;
   }
   else if (state) fcntl(ossaudio->fd, F_SETFL, fcntl(ossaudio->fd, F_GETFL) | O_NONBLOCK);
   else       fcntl(ossaudio->fd, F_SETFL, fcntl(ossaudio->fd, F_GETFL) & (~O_NONBLOCK));

   ossaudio->nonblock = state;
}

static void oss_free(void *data)
{
   oss_audio_t *ossaudio  = (oss_audio_t*)data;

   close(ossaudio->fd);
   free(data);
}

static size_t oss_write_avail(void *data)
{
   audio_buf_info info = {0};
   oss_audio_t *ossaudio = (oss_audio_t*)data;
   if (ossaudio->fifo_limit)
      return miyoo_fifo_write_avail(ossaudio->fd, ossaudio->fifo_limit);
   if (ossaudio->capacity == 2 ||
       ioctl(ossaudio->fd, SNDCTL_DSP_GETOSPACE, &info) < 0 || info.bytes < 0)
      return ossaudio->capacity / 2;
   return (size_t)info.bytes < ossaudio->capacity ?
         (size_t)info.bytes : ossaudio->capacity;
}

static size_t oss_buffer_size(void *data)
{
   oss_audio_t *ossaudio = (oss_audio_t*)data;
   return ossaudio->fifo_limit ? ossaudio->fifo_limit : ossaudio->capacity;
}

static bool oss_use_float(void *data)
{
   (void)data;
   return false;
}

audio_driver_t audio_oss = {
   oss_init,
   oss_write,
   oss_stop,
   oss_start,
   oss_alive,
   oss_set_nonblock_state,
   oss_free,
   oss_use_float,
   "oss",
   NULL,
   NULL,
   oss_write_avail,
   oss_buffer_size,
};
