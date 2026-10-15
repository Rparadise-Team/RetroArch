/*  RetroArch - A frontend for libretro.
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

/* GBAOffset: GBA frame (240x160) on a fixed 240x180 output with black
 * lines below, used by the Koriki GBA overlays. Built-in replacement of
 * the external gbaoffset.so (same ident and output), using row memcpy. */

#include "softfilter.h"
#include <stdlib.h>
#include <string.h>

#ifdef RARCH_INTERNAL
#define softfilter_get_implementation gbaoffset_get_implementation
#define softfilter_thread_data gbaoffset_softfilter_thread_data
#define filter_data gbaoffset_filter_data
#endif

#define GBAOFFSET_OUT_WIDTH  240
#define GBAOFFSET_OUT_HEIGHT 180

struct softfilter_thread_data
{
   void *out_data;
   const void *in_data;
   size_t out_pitch;
   size_t in_pitch;
   unsigned width;
   unsigned height;
   unsigned bpp;
};

struct filter_data
{
   unsigned threads;
   struct softfilter_thread_data *workers;
   unsigned in_fmt;
};

static unsigned gbaoffset_generic_input_fmts(void)
{
   return SOFTFILTER_FMT_XRGB8888 | SOFTFILTER_FMT_RGB565;
}

static unsigned gbaoffset_generic_output_fmts(unsigned input_fmts)
{
   return input_fmts;
}

static unsigned gbaoffset_generic_threads(void *data)
{
   struct filter_data *filt = (struct filter_data*)data;
   return filt->threads;
}

static void *gbaoffset_generic_create(const struct softfilter_config *config,
      unsigned in_fmt, unsigned out_fmt,
      unsigned max_width, unsigned max_height,
      unsigned threads, softfilter_simd_mask_t simd, void *userdata)
{
   struct filter_data *filt = (struct filter_data*)calloc(1, sizeof(*filt));
   if (!filt)
      return NULL;
   if (!(filt->workers = (struct softfilter_thread_data*)calloc(1, sizeof(struct softfilter_thread_data))))
   {
      free(filt);
      return NULL;
   }
   /* A row copy is memory bound: one thread is enough */
   filt->threads = 1;
   filt->in_fmt  = in_fmt;
   return filt;
}

static void gbaoffset_generic_output(void *data,
      unsigned *out_width, unsigned *out_height,
      unsigned width, unsigned height)
{
   *out_width  = GBAOFFSET_OUT_WIDTH;
   *out_height = GBAOFFSET_OUT_HEIGHT;
}

static void gbaoffset_generic_destroy(void *data)
{
   struct filter_data *filt = (struct filter_data*)data;
   if (!filt)
      return;
   free(filt->workers);
   free(filt);
}

static void gbaoffset_work_cb(void *data, void *thread_data)
{
   struct softfilter_thread_data *thr = (struct softfilter_thread_data*)thread_data;
   const uint8_t *input = (const uint8_t*)thr->in_data;
   uint8_t *output      = (uint8_t*)thr->out_data;
   unsigned width       = thr->width  < GBAOFFSET_OUT_WIDTH  ? thr->width  : GBAOFFSET_OUT_WIDTH;
   unsigned height      = thr->height < GBAOFFSET_OUT_HEIGHT ? thr->height : GBAOFFSET_OUT_HEIGHT;
   size_t copy_bytes    = (size_t)width * thr->bpp;
   size_t row_bytes     = (size_t)GBAOFFSET_OUT_WIDTH * thr->bpp;
   unsigned y;

   for (y = 0; y < height; y++)
   {
      memcpy(output, input, copy_bytes);
      if (copy_bytes < row_bytes)
         memset(output + copy_bytes, 0, row_bytes - copy_bytes);
      input  += thr->in_pitch;
      output += thr->out_pitch;
   }

   /* Black lines below the picture */
   for (; y < GBAOFFSET_OUT_HEIGHT; y++)
   {
      memset(output, 0, row_bytes);
      output += thr->out_pitch;
   }
}

static void gbaoffset_generic_packets(void *data,
      struct softfilter_work_packet *packets,
      void *output, size_t output_stride,
      const void *input, unsigned width, unsigned height, size_t input_stride)
{
   struct filter_data *filt           = (struct filter_data*)data;
   struct softfilter_thread_data *thr = (struct softfilter_thread_data*)&filt->workers[0];

   thr->out_data  = (uint8_t*)output;
   thr->in_data   = (const uint8_t*)input;
   thr->out_pitch = output_stride;
   thr->in_pitch  = input_stride;
   thr->width     = width;
   thr->height    = height;
   thr->bpp       = (filt->in_fmt == SOFTFILTER_FMT_XRGB8888) ? 4 : 2;

   packets[0].work        = gbaoffset_work_cb;
   packets[0].thread_data = thr;
}

static const struct softfilter_implementation gbaoffset_generic = {
   gbaoffset_generic_input_fmts,
   gbaoffset_generic_output_fmts,

   gbaoffset_generic_create,
   gbaoffset_generic_destroy,

   gbaoffset_generic_threads,
   gbaoffset_generic_output,
   gbaoffset_generic_packets,

   SOFTFILTER_API_VERSION,
   "GBAOffset",
   "gbaoffset",
};

const struct softfilter_implementation *softfilter_get_implementation(
      softfilter_simd_mask_t simd)
{
   return &gbaoffset_generic;
}

#ifdef RARCH_INTERNAL
#undef softfilter_get_implementation
#undef softfilter_thread_data
#undef filter_data
#endif
