/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2018 - Daniel De Matteis
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

/* Compile: gcc -o vertical3x4.so -shared vertical3x4.c -std=c99 -O3 -Wall -pedantic -fPIC */

#include "softfilter.h"
#include <stdlib.h>
#include <string.h>

#ifdef RARCH_INTERNAL
#define softfilter_get_implementation vertical3x4_get_implementation
#define softfilter_thread_data vertical3x4_softfilter_thread_data
#define filter_data vertical3x4_filter_data
#endif

struct softfilter_thread_data
{
   void *out_data;
   const void *in_data;
   size_t out_pitch;
   size_t in_pitch;
   unsigned colfmt;
   unsigned width;
   unsigned height;
   int first;
   int last;
};

struct filter_data
{
   unsigned threads;
   struct softfilter_thread_data *workers;
   unsigned in_fmt;
};

static unsigned vertical3x4_generic_input_fmts(void)
{
   return SOFTFILTER_FMT_XRGB8888 | SOFTFILTER_FMT_RGB565;
}

static unsigned vertical3x4_generic_output_fmts(unsigned input_fmts)
{
   return input_fmts;
}

static unsigned vertical3x4_generic_threads(void *data)
{
   struct filter_data *filt = (struct filter_data*)data;
   return filt->threads;
}

static void *vertical3x4_generic_create(const struct softfilter_config *config,
      unsigned in_fmt, unsigned out_fmt,
      unsigned max_width, unsigned max_height,
      unsigned threads, softfilter_simd_mask_t simd, void *userdata)
{
   struct filter_data *filt = (struct filter_data*)calloc(1, sizeof(*filt));
   (void)simd;
   (void)config;
   (void)userdata;

   if (!filt) {
      return NULL;
   }
   /* Apparently the code is not thread-safe,
    * so force single threaded operation... */
   filt->workers = (struct softfilter_thread_data*)calloc(1, sizeof(struct softfilter_thread_data));
   filt->threads = 1;
   filt->in_fmt  = in_fmt;
   if (!filt->workers) {
      free(filt);
      return NULL;
   }
   return filt;
}

static void vertical3x4_generic_output(void *data,
      unsigned *out_width, unsigned *out_height,
      unsigned width, unsigned height)
{
   *out_width = width;
   *out_height = height + ((((height << 4) / 9) - height) & ~1);
}

static void vertical3x4_generic_destroy(void *data)
{
   struct filter_data *filt = (struct filter_data*)data;
   if (!filt) {
      return;
   }
   free(filt->workers);
   free(filt);
}

static void vertical3x4_work_cb(void *data, void *thread_data)
{
   struct softfilter_thread_data *thr = (struct softfilter_thread_data*)thread_data;
   const void* src = thr->in_data;
   void* dst = thr->out_data;
   unsigned ip = thr->in_pitch;
   unsigned op = thr->out_pitch;
   unsigned ch = thr->height;
   unsigned bh = (((ch << 4) / 9) - ch) >> 1;

   if (ip == op) {
      unsigned cs = op * ch;
      if (bh) {
         unsigned bs = op * bh;
         memset(dst, 0, bs); dst += bs;
         memcpy(dst, src, cs); dst += cs;
         memset(dst, 0, bs);
      } else {
         memcpy(dst, src, cs);
      }
   } else {
      unsigned i;
      if (bh) {
         unsigned bs = op * bh;
         memset(dst, 0, bs); dst += bs;
         for (i=ch; i>0; i--, src += ip, dst += op) memcpy(dst, src, ip);
         memset(dst, 0, bs);
      } else {
         for (i=ch; i>0; i--, src += ip, dst += op) memcpy(dst, src, ip);
      }
   }
}

static void vertical3x4_generic_packets(void *data,
      struct softfilter_work_packet *packets,
      void *output, size_t output_stride,
      const void *input, unsigned width, unsigned height, size_t input_stride)
{
   /* We are guaranteed single threaded operation
    * (filt->threads = 1) so we don't need to loop
    * over threads and can cull some code. This only
    * makes the tiniest performance difference, but
    * every little helps when running on an o3DS... */
   struct filter_data *filt = (struct filter_data*)data;
   struct softfilter_thread_data *thr = (struct softfilter_thread_data*)&filt->workers[0];

   thr->out_data = (uint8_t*)output;
   thr->in_data = (const uint8_t*)input;
   thr->out_pitch = output_stride;
   thr->in_pitch = input_stride;
   thr->width = width;
   thr->height = height;

   packets[0].work = vertical3x4_work_cb;
   packets[0].thread_data = thr;
}

static const struct softfilter_implementation vertical3x4_generic = {
   vertical3x4_generic_input_fmts,
   vertical3x4_generic_output_fmts,

   vertical3x4_generic_create,
   vertical3x4_generic_destroy,

   vertical3x4_generic_threads,
   vertical3x4_generic_output,
   vertical3x4_generic_packets,

   SOFTFILTER_API_VERSION,
   "Vertical3x4",
   "vertical3x4",
};

const struct softfilter_implementation *softfilter_get_implementation(
      softfilter_simd_mask_t simd)
{
   (void)simd;
   return &vertical3x4_generic;
}

#ifdef RARCH_INTERNAL
#undef softfilter_get_implementation
#undef softfilter_thread_data
#undef filter_data
#endif
