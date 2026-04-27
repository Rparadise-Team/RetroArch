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
      SDL audio driver for miyoomini customSDL
      Can be used with standard SDL as well
*/

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <boolean.h>
#include <rthreads/rthreads.h>
#include <queues/fifo_queue.h>
#include <retro_inline.h>
#include <retro_math.h>

#include "SDL.h"
#include "SDL_audio.h"

#include "../audio_driver.h"
#include "../../verbosity.h"
#include "volume/volume.h"
#include <mi_ao.h>

#define SDL_AUDIO_SAMPLES 256
#define AUDIOSERVER_FIFO "/tmp/audio_fifo_server"

static unsigned sdl_audio_mini_supported_rate(unsigned rate)
{
   static const unsigned supported_rates[] = {
      8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
   };
   unsigned i;

   for (i = 0; i < sizeof(supported_rates) / sizeof(supported_rates[0]); i++)
      if (rate <= supported_rates[i])
         return supported_rates[i];

   return supported_rates[(sizeof(supported_rates) / sizeof(supported_rates[0])) - 1];
}

static void sdl_audio_miyoo_prime_mi_ao(const SDL_AudioSpec *spec)
{
   MI_AUDIO_Attr_t attr;
   MI_AUDIO_Frame_t frame;
   uint32_t num_frames;
   uint32_t i;
   size_t frame_bytes;
   void *silence;

   memset(&attr, 0, sizeof(attr));
   memset(&frame, 0, sizeof(frame));

   attr.eSamplerate   = (MI_AUDIO_SampleRate_e)spec->freq;
   attr.eSoundmode    = (spec->channels == 2) ?
         E_MI_AUDIO_SOUND_MODE_STEREO : E_MI_AUDIO_SOUND_MODE_MONO;
   attr.u32ChnCnt     = spec->channels;
   attr.u32PtNumPerFrm = spec->samples;

   if (MI_AO_SetPubAttr(0, &attr) != MI_SUCCESS)
   {
      RARCH_WARN("[SDL audio]: MI_AO_SetPubAttr failed while priming AO.\n");
      return;
   }

   MI_AO_Enable(0);
   MI_AO_EnableChn(0, 0);

   frame_bytes     = spec->samples * spec->channels * sizeof(int16_t);
   silence         = calloc(1, frame_bytes);
   if (!silence)
      return;

   frame.apVirAddr[0] = silence;
   frame.u32Len       = frame_bytes;

   num_frames = (uint32_t)((spec->freq - 1) / (50 * spec->samples)) + 1;
   if (num_frames < 2)
      num_frames = 2;

   MI_AO_ClearChnBuf(0, 0);
   for (i = num_frames; i > 0; i--)
      MI_AO_SendFrame(0, 0, &frame, 0);

   free(silence);
}

typedef struct sdl_audio
{
#ifdef HAVE_THREADS
   slock_t *lock;
   scond_t *cond;
#endif
   fifo_buffer_t *buffer;
   bool nonblock;
   bool is_paused;
   size_t bufsize;
   int audiofix;
   bool audioserver_mode;
   int audioserver_fd;
} sdl_audio_t;

static void sdl_audio_cb(void *data, Uint8 *stream, int len)
{
   sdl_audio_t *sdl = (sdl_audio_t*)data;
   size_t avail = FIFO_READ_AVAIL(sdl->buffer);

#ifdef NO_MMP
   size_t write_size = len > (int)avail ? avail : len;
   fifo_read(sdl->buffer, stream, write_size);
   if (len > (int)write_size)
      memset(stream + write_size, 0, len - write_size);
#else
   if (avail < (size_t)len / 2)
   {
      memset(stream, 0, len);
      if (avail > 0)
         fifo_read(sdl->buffer, stream, avail);
   }
   else
   {
      size_t write_size = len > (int)avail ? avail : len;
      fifo_read(sdl->buffer, stream, write_size);
      if (len > (int)write_size)
         memset(stream + write_size, 0, len - write_size);
   }
#endif
#ifdef HAVE_THREADS
   scond_signal(sdl->cond);
#endif
}

static void *sdl_audio_init(const char *device,
      unsigned rate, unsigned latency,
      unsigned block_frames,
      unsigned *new_rate)
{
   int frames;
   SDL_AudioSpec out;
   SDL_AudioSpec spec           = {0};
   void *tmp                    = NULL;
   sdl_audio_t *sdl             = NULL;
   uint32_t sdl_subsystem_flags = SDL_WasInit(0);
   int target_vol               = 0;
   int brightnessMM             = 0;
   char command2[100];
   unsigned min_samples         = 0;

   (void)device;

   /* Initialise audio subsystem, if required */
   if (sdl_subsystem_flags == 0)
   {
      if (SDL_Init(SDL_INIT_AUDIO) < 0)
         return NULL;
   }
   else if ((sdl_subsystem_flags & SDL_INIT_AUDIO) == 0)
   {
      if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
         return NULL;
   }

   sdl = (sdl_audio_t*)calloc(1, sizeof(*sdl));
   if (!sdl)
      return NULL;

   spec.freq     = sdl_audio_mini_supported_rate(rate);
   min_samples   = next_pow2((spec.freq + 99) / 100);
   spec.format   = AUDIO_S16SYS;
   spec.channels = 2;
   spec.samples  = (min_samples > SDL_AUDIO_SAMPLES) ? min_samples : SDL_AUDIO_SAMPLES;
   if (spec.samples > 2048)
      spec.samples = 2048;
   else if (spec.samples < 8)
      spec.samples = 8;
   spec.callback = sdl_audio_cb;
   spec.userdata = sdl;

   sdl->audiofix = getValueMM("audiofix");
   sdl->audioserver_mode = (sdl->audiofix != 0);
   sdl->audioserver_fd   = -1;

   if (sdl->audioserver_mode)
   {
      sdl->audioserver_fd = open(AUDIOSERVER_FIFO, O_WRONLY);
      if (sdl->audioserver_fd < 0)
      {
         RARCH_ERR("[SDL audio]: Cannot open audioserver FIFO: %s\n", AUDIOSERVER_FIFO);
         goto error;
      }
      *new_rate = spec.freq;
      sdl->bufsize = (latency * spec.freq / 1000) << 2;
      RARCH_LOG("[SDL audio]: with audioserver FIFO mode\n");
      goto platform_init;
   }

   if (SDL_OpenAudio(&spec, &out) < 0)
   {
      RARCH_ERR("[SDL audio]: Failed to open SDL audio: %s\n", SDL_GetError());
      goto error;
   }

   sdl_audio_miyoo_prime_mi_ao(&out);

#ifdef HAVE_THREADS
   sdl->lock = slock_new();
   sdl->cond = scond_new();
#endif

   *new_rate = out.freq;
   frames    = (latency * (out.freq - 1)) / (1000 * out.samples) + 1;
   if (frames < 2) frames = 2; /* at least 2 frames */

   RARCH_LOG("[SDL audio]: Requested %u ms latency, got %d ms\n",
         latency, (int)(out.samples * frames * 1000 / (*new_rate)));

   /* Create a buffer twice as big as needed */
   sdl->bufsize = out.samples * out.channels * sizeof(int16_t) * frames * 2;
   sdl->buffer  = fifo_new(sdl->bufsize);

   /* Allocate the null-buffer and prefill */
#ifdef NO_MMP
   size_t prefill_size = (sdl->bufsize * 7) / 10;
#else
   size_t prefill_size = (sdl->bufsize * 3) / 4;
#endif
   tmp = calloc(1, prefill_size);
   if (tmp) { fifo_write(sdl->buffer, tmp, prefill_size); free(tmp); }

   SDL_PauseAudio(0);

platform_init:
   /*set volumen */
   target_vol = getVolumeMM();
   set_snd_level(target_vol);
   brightnessMM = setBrightnessMM();
   sprintf(command2, "echo %d > /sys/class/pwm/pwmchip0/pwm0/duty_cycle", brightnessMM);
   system(command2);

   if (sdl->audiofix == 0) {
      RARCH_LOG("[SDL audio]: without audioserver\n");
   } else {
      RARCH_LOG("[SDL audio]: with audioserver\n");
   }

   return sdl;

error:
   free(sdl);
   return NULL;
}

static ssize_t sdl_audio_write(void *data, const void *buf, size_t size)
{
   ssize_t ret      = 0;
   sdl_audio_t *sdl = (sdl_audio_t*)data;

   if (sdl->audioserver_mode && sdl->audioserver_fd >= 0)
   {
      size_t total = 0;
      while (total < size)
      {
         ssize_t w = write(sdl->audioserver_fd, (const uint8_t*)buf + total, size - total);
         if (w > 0)
            total += w;
         else if (w < 0 && errno == EAGAIN)
            usleep(2000);
         else
            break;
      }
      return total;
   }

   if (sdl->nonblock)
   {
      size_t avail, write_amt;

      SDL_LockAudio();
      avail = FIFO_WRITE_AVAIL(sdl->buffer);
      write_amt = avail > size ? size : avail;
      fifo_write(sdl->buffer, buf, write_amt);
      SDL_UnlockAudio();
      ret = write_amt;
   }
   else
   {
      size_t written = 0;
      while (written < size)
      {
         size_t avail;
         SDL_LockAudio();
         avail = FIFO_WRITE_AVAIL(sdl->buffer);
#ifdef NO_MMP
         if (avail < (sdl->bufsize/2))
#else
         if (avail < (sdl->bufsize/3))
#endif
         {
            SDL_UnlockAudio();
#ifdef HAVE_THREADS
            slock_lock(sdl->lock);
            scond_wait(sdl->cond, sdl->lock);
            slock_unlock(sdl->lock);
#else
            SDL_Delay(1);
#endif
         }
         else
         {
            size_t write_amt = size - written > avail ? avail : size - written;
            size_t current_avail;
            fifo_write(sdl->buffer, (const char*)buf + written, write_amt);
            SDL_UnlockAudio();
            written += write_amt;
            current_avail = avail - write_amt;

            /* FIX: Delay adaptativo para AudioServer OFF
             * Si buffer está muy lleno, esperar un poco antes de siguiente write
             * Esto sincroniza mejor con el callback y evita acumulación
             */
#ifdef NO_MMP
            if (current_avail < (sdl->bufsize/3))
            {
               /* Buffer muy lleno, dormir un poco */
               SDL_Delay(2);  /* 2ms es imperceptible pero da tiempo */
            }
#else
            if (current_avail < (sdl->bufsize/4))
            {
               /* Buffer muy lleno, dormir un poco */
               SDL_Delay(1);  /* 1ms es imperceptible pero da tiempo */
            }
#endif
         }
      }
      ret = written;
   }
   return ret;
}

static bool sdl_audio_stop(void *data)
{
   sdl_audio_t *sdl = (sdl_audio_t*)data;
   if (!sdl->audioserver_mode)
      SDL_PauseAudio(1);
   sdl->is_paused = true;
   return true;
}

static bool sdl_audio_alive(void *data)
{
   sdl_audio_t *sdl = (sdl_audio_t*)data;
   return !sdl->is_paused;
}

static bool sdl_audio_start(void *data, bool is_shutdown)
{
   sdl_audio_t *sdl = (sdl_audio_t*)data;
   if (!sdl->audioserver_mode)
      SDL_PauseAudio(0);
   sdl->is_paused = false;
   return true;
}

static void sdl_audio_set_nonblock_state(void *data, bool state)
{
   sdl_audio_t *sdl = (sdl_audio_t*)data;
   sdl->nonblock = state;
}

static void sdl_audio_free(void *data)
{
   sdl_audio_t *sdl = (sdl_audio_t*)data;

   if (sdl->audioserver_mode)
   {
      if (sdl->audioserver_fd >= 0)
         close(sdl->audioserver_fd);
   }
   else
   {
      SDL_CloseAudio();
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      MI_AO_ClearChnBuf(0, 0);
      MI_AO_DisableChn(0, 0);
      MI_AO_Disable(0);
   }

   if (sdl->buffer)
      fifo_free(sdl->buffer);
#ifdef HAVE_THREADS
   slock_free(sdl->lock);
   scond_free(sdl->cond);
#endif
   free(sdl);
}

static bool sdl_audio_use_float(void *data)
{
   (void)data;
   return false;
}

static size_t sdl_audio_write_avail(void *data)
{
   sdl_audio_t *sdl = (sdl_audio_t*)data;
   if (sdl->audioserver_mode)
      return sdl->bufsize ? sdl->bufsize : 1024;
   SDL_LockAudio();
   size_t avail = FIFO_WRITE_AVAIL(sdl->buffer);
   SDL_UnlockAudio();
   return avail;
}

static size_t sdl_audio_buffer_size(void *data)
{
   sdl_audio_t *sdl = (sdl_audio_t*)data;
   if (sdl->audioserver_mode)
      return sdl->bufsize;
   return sdl->bufsize;
}

audio_driver_t audio_sdl = {
   sdl_audio_init,
   sdl_audio_write,
   sdl_audio_stop,
   sdl_audio_start,
   sdl_audio_alive,
   sdl_audio_set_nonblock_state,
   sdl_audio_free,
   sdl_audio_use_float,
   "sdl2",
   NULL,
   NULL,
   sdl_audio_write_avail,
   sdl_audio_buffer_size,
};
