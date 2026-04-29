/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *  Copyright (C) 2011-2017 - Higor Euripedes
 *  Copyright (C) 2019-2021 - James Leaver
 *  Copyright (C)      2021 - John Parton
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

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>

#include <gfx/video_frame.h>
#include <string/stdstring.h>
#include <encodings/utf.h>
#include <features/features_cpu.h>
#include <formats/rpng.h>
#include <streams/file_stream.h>

#include "gfx.c"
#include "scaler_neon.c"
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

#ifdef HAVE_CHEEVOS
#include "../../cheevos/cheevos.h"
#endif

#ifdef HAVE_MENU
#include "../../menu/menu_driver.h"
#endif

#include "../../dingux/dingux_utils.h"

#include "../../verbosity.h"
#include "../../gfx/drivers_font_renderer/bitmap.h"
#include "../../gfx_widgets.h"
#include "../../configuration.h"
#include "../../file_path_special.h"
#include "../../paths.h"
#include "../../retroarch.h"
#include "../../runloop.h"
#if defined(MIYOO_CUSTOM_MENU)
#include "../../../miyoo.h"
#endif

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define SDL_MIYOOMINI_WIDTH  640
#define SDL_MIYOOMINI_HEIGHT 480
#define RGUI_MENU_WIDTH  320
#define RGUI_MENU_HEIGHT 240
#define SDL_NUM_FONT_GLYPHS 256
#define OSD_TEXT_Y_MARGIN 4
#define OSD_TEXT_LINES_MAX 3	/* 1 .. 7 */
#define OSD_TEXT_LINE_LEN ((uint32_t)(RGUI_MENU_WIDTH / FONT_WIDTH_STRIDE)-1)
#define OSD_TEXT_LEN_MAX (OSD_TEXT_LINE_LEN * OSD_TEXT_LINES_MAX)
#define CHEEVOS_ICON_DURATION_FRAMES (3 * 60)

typedef struct sdl_miyoomini_video sdl_miyoomini_video_t;
struct sdl_miyoomini_video
{
   SDL_Surface *screens[2];
   SDL_Surface *screen;
   uint16_t screen_fence[2];
   unsigned screen_index;
   void (*scale_func)(void* data, void* __restrict src, void* __restrict dst, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dp);
   /* Scaling/padding/cropping parameters */
   unsigned content_width;
   unsigned content_height;
   unsigned frame_width;
   unsigned frame_height;
   unsigned video_x;
   unsigned video_y;
   unsigned video_w;
   unsigned video_h;
   unsigned rotate;
   bool rgb32;
   bool menu_active;
   bool was_in_menu;
   retro_time_t last_frame_time;
   retro_time_t ff_frame_time_min;
   float ff_refresh_rate_cached;
   enum dingux_ipu_filter_type filter_type;
   bool vsync;
   bool keep_aspect;
   bool scale_integer;
   bool quitting;
   bitmapfont_lut_t *osd_font;
   uint32_t font_colour32;
   SDL_Surface *menuscreen;
   SDL_Surface *menuscreen_rgui;
   uint16_t menu_bg_texture[RGUI_MENU_WIDTH * RGUI_MENU_HEIGHT];
   uint16_t menu_composite_texture[RGUI_MENU_WIDTH * RGUI_MENU_HEIGHT];
   float menu_texture_alpha;
   uint8_t menu_texture_alpha_u8;
   bool menu_bg_valid;
   bool menu_surface_dirty;
   bool menu_composite_valid;
#ifdef HAVE_OVERLAY
   SDL_Surface *overlay_surface;
#endif
   unsigned msg_count;
   bool flip_callback_active;
   bool msg_bg_enable_cached;
   uint8_t msg_bg_alpha_cached;
   uint32_t msg_bg_rgb_cached;
   char msg_tmp[OSD_TEXT_LEN_MAX];
   uint32_t msg_len_cached;
   uint32_t msg_line_count_cached;
   unsigned msg_line_chars_cached[OSD_TEXT_LINES_MAX];
   unsigned menu_src_width_cached;
   unsigned menu_src_height_cached;
   bool menu_src_rgb32_cached;
   unsigned menu_x_step_cached;
   unsigned menu_y_step_cached;
#ifdef HAVE_CHEEVOS
   SDL_mutex *cheevos_lock;
   uint32_t *cheevos_icon_data;
   unsigned cheevos_icon_width;
   unsigned cheevos_icon_height;
   unsigned cheevos_icon_size;
   bool cheevos_icon_visible;
   unsigned cheevos_icon_timer;
   bool cheevos_icon_background_stored;
   bool cheevos_icon_restore_pending;
   void *cheevos_icon_restore_data;
   size_t cheevos_icon_restore_pitch;
   unsigned cheevos_icon_restore_width;
   unsigned cheevos_icon_restore_height;
   int cheevos_icon_restore_x;
   int cheevos_icon_restore_y;
   unsigned cheevos_icon_retry_counter;
   char cheevos_badge_name[32];
   char cheevos_badge_pending[32];
#endif
};

static INLINE uint16_t sdl_miyoomini_blend_565(uint16_t bg, uint16_t fg, uint8_t alpha)
{
   uint32_t inv_alpha = 255 - alpha;
   uint32_t bg_r      = (bg >> 11) & 0x1F;
   uint32_t bg_g      = (bg >> 5)  & 0x3F;
   uint32_t bg_b      = bg & 0x1F;
   uint32_t fg_r      = (fg >> 11) & 0x1F;
   uint32_t fg_g      = (fg >> 5)  & 0x3F;
   uint32_t fg_b      = fg & 0x1F;
   uint32_t out_r     = ((fg_r * alpha) + (bg_r * inv_alpha) + 127) / 255;
   uint32_t out_g     = ((fg_g * alpha) + (bg_g * inv_alpha) + 127) / 255;
   uint32_t out_b     = ((fg_b * alpha) + (bg_b * inv_alpha) + 127) / 255;

   return (uint16_t)((out_r << 11) | (out_g << 5) | out_b);
}

static uint16_t sdl_miyoomini_surface_read_pixel565(
      const SDL_Surface *surface, unsigned x, unsigned y)
{
   const SDL_PixelFormat *fmt = surface->format;
   const uint8_t *row         = (const uint8_t*)surface->pixels + (y * surface->pitch);
   const uint8_t *src         = row + (x * fmt->BytesPerPixel);

   switch (fmt->BytesPerPixel)
   {
      case 2:
         return *(const uint16_t*)src;
      case 4:
      {
         uint32_t pixel = *(const uint32_t*)src;
         uint8_t r, g, b;
         SDL_GetRGB(pixel, (SDL_PixelFormat*)fmt, &r, &g, &b);
         return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      }
      default:
         break;
   }

   return 0;
}

static void sdl_miyoomini_apply_state_changes(void *data);
static void sdl_miyoomini_init_font_color(sdl_miyoomini_video_t *vid);
static void sdl_miyoomini_update_msg_bg_cache(
      sdl_miyoomini_video_t *vid, const settings_t *settings);
static void sdl_miyoomini_update_msg_cache(
      sdl_miyoomini_video_t *vid, const char *msg);

static void sdl_miyoomini_capture_menu_background(sdl_miyoomini_video_t *vid)
{
   unsigned x, y;
   SDL_Surface *screen = vid->screen;

   if (!screen || !screen->pixels || !screen->w || !screen->h)
      return;

   if (SDL_MUSTLOCK(screen))
      SDL_LockSurface(screen);

   for (y = 0; y < RGUI_MENU_HEIGHT; y++)
   {
      unsigned src_y = (y * (unsigned)screen->h) / RGUI_MENU_HEIGHT;
      for (x = 0; x < RGUI_MENU_WIDTH; x++)
      {
         unsigned src_x = (x * (unsigned)screen->w) / RGUI_MENU_WIDTH;
         vid->menu_bg_texture[(y * RGUI_MENU_WIDTH) + x] =
               sdl_miyoomini_surface_read_pixel565(screen, src_x, src_y);
      }
   }

   if (SDL_MUSTLOCK(screen))
      SDL_UnlockSurface(screen);

   vid->menu_bg_valid = true;
   vid->menu_surface_dirty = true;
   vid->menu_composite_valid = false;
}

#ifdef HAVE_CHEEVOS

static bool sdl_miyoomini_load_png_argb(const char *path, uint32_t **data,
      unsigned *width, unsigned *height)
{
   rpng_t *rpng                 = NULL;
   void *file_data              = NULL;
   int64_t file_len             = 0;
   int process_ret              = 0;
   bool success                 = false;

   if (filestream_read_file(path, &file_data, &file_len) <= 0 || !file_data)
      return false;


   rpng = rpng_alloc();
   if (!rpng)
      goto cleanup;

   if (!rpng_set_buf_ptr(rpng, (uint8_t*)file_data, (size_t)file_len))
      goto cleanup;

   if (!rpng_start(rpng))
      goto cleanup;

   while (rpng_iterate_image(rpng));

   if (!rpng_is_valid(rpng))
      goto cleanup;

   do
   {
      process_ret = rpng_process_image(rpng, (void**)data,
            (size_t)file_len, width, height);
   } while (process_ret == IMAGE_PROCESS_NEXT);

   if (process_ret == IMAGE_PROCESS_ERROR || process_ret == IMAGE_PROCESS_ERROR_END)
      goto cleanup;

   success = true;

cleanup:
   if (rpng)
      rpng_free(rpng);
   if (!success && data && *data)
   {
      free(*data);
      *data = NULL;
   }
   free(file_data);

   return success;
}

static INLINE void sdl_miyoomini_cheevos_lock(sdl_miyoomini_video_t *vid)
{
   if (vid && vid->cheevos_lock)
      SDL_LockMutex(vid->cheevos_lock);
}

static INLINE void sdl_miyoomini_cheevos_unlock(sdl_miyoomini_video_t *vid)
{
   if (vid && vid->cheevos_lock)
      SDL_UnlockMutex(vid->cheevos_lock);
}

static void sdl_miyoomini_free_cheevos_icon_nolock(sdl_miyoomini_video_t *vid)
{
   if (!vid)
      return;


   free(vid->cheevos_icon_data);
   free(vid->cheevos_icon_restore_data);
   vid->cheevos_icon_data   = NULL;
   vid->cheevos_icon_width  = 0;
   vid->cheevos_icon_height = 0;
   vid->cheevos_icon_size   = 0;
   vid->cheevos_icon_timer  = 0;
   vid->cheevos_icon_background_stored = false;
   vid->cheevos_icon_restore_pending = false;
   vid->cheevos_icon_restore_data = NULL;
   vid->cheevos_icon_restore_pitch = 0;
   vid->cheevos_icon_restore_width = 0;
   vid->cheevos_icon_restore_height = 0;
   vid->cheevos_icon_restore_x = 0;
   vid->cheevos_icon_restore_y = 0;
   vid->cheevos_icon_retry_counter = 0;
   vid->cheevos_badge_name[0] = '\0';
   vid->cheevos_badge_pending[0] = '\0';
}

static void sdl_miyoomini_free_cheevos_icon(sdl_miyoomini_video_t *vid)
{
   sdl_miyoomini_cheevos_lock(vid);
   sdl_miyoomini_free_cheevos_icon_nolock(vid);
   sdl_miyoomini_cheevos_unlock(vid);
}

static unsigned sdl_miyoomini_get_cheevos_icon_size(unsigned screen_height)
{
   return (screen_height >= 480) ? 104 : 72;
}

static void sdl_miyoomini_scale_argb(const uint32_t *src, unsigned src_w, unsigned src_h,
      uint32_t *dst, unsigned dst_w, unsigned dst_h)
{
   unsigned y;
   for (y = 0; y < dst_h; y++)
   {
      unsigned x;
      unsigned src_y = (y * src_h) / dst_h;
      for (x = 0; x < dst_w; x++)
      {
         unsigned src_x = (x * src_w) / dst_w;
         dst[y * dst_w + x] = src[src_y * src_w + src_x];
      }
   }
}

static void sdl_miyoomini_append_cheevos_badge_subdir(char *badge_dir,
      size_t len, const char *badge_name)
{
#if defined(HAVE_CHEEVOS) && defined(MIYOO_CUSTOM_MENU)
   char game_badge_url[PATH_MAX_LENGTH];
   const char *badge_file = NULL;
   char game_badge_dir[32];
   char base_dir[PATH_MAX_LENGTH];
   size_t badge_id_len;

   if (string_is_empty(badge_name) || string_is_equal(badge_name, "00000"))
      return;

   if (!rcheevos_get_game_badge_url(game_badge_url, sizeof(game_badge_url)))
      return;

   badge_file = strrchr(game_badge_url, '/');
   badge_file = badge_file ? badge_file + 1 : game_badge_url;

   badge_id_len = strcspn(badge_file, ".?");
   if (badge_id_len == 0 || badge_id_len > (sizeof(game_badge_dir) - 2))
      return;

   game_badge_dir[0] = 'i';
   memcpy(&game_badge_dir[1], badge_file, badge_id_len);
   game_badge_dir[badge_id_len + 1] = '\0';

   strlcpy(base_dir, badge_dir, sizeof(base_dir));
   fill_pathname_join_special(badge_dir, base_dir, game_badge_dir, len);
#else
   (void)badge_dir;
   (void)len;
   (void)badge_name;
#endif
}

static bool sdl_miyoomini_load_cheevos_icon(sdl_miyoomini_video_t *vid,
      const char *badge_name, unsigned screen_height)
{
   char badge_path[PATH_MAX_LENGTH];
   uint32_t *icon_data = NULL;
   uint32_t *scaled_data = NULL;
   unsigned icon_width = 0;
   unsigned icon_height = 0;
   unsigned target_size = sdl_miyoomini_get_cheevos_icon_size(screen_height);

   if (!vid || string_is_empty(badge_name))
      return false;

   if (vid->cheevos_icon_data
         && string_is_equal(vid->cheevos_badge_name, badge_name))
      return true;


   fill_pathname_application_special(badge_path, sizeof(badge_path),
         APPLICATION_SPECIAL_DIRECTORY_THUMBNAILS_CHEEVOS_BADGES);
   sdl_miyoomini_append_cheevos_badge_subdir(
         badge_path, sizeof(badge_path), badge_name);
   fill_pathname_slash(badge_path, sizeof(badge_path));
   strlcat(badge_path, badge_name, sizeof(badge_path));
   strlcat(badge_path, FILE_PATH_PNG_EXTENSION, sizeof(badge_path));

   /* Badge download requests are handled by the cheevos subsystem.
    * Driver only loads from disk to avoid duplicate/racy fetch paths. */
   if (!path_is_valid(badge_path))
   {
      return false;
   }

   if (!sdl_miyoomini_load_png_argb(badge_path, &icon_data, &icon_width, &icon_height))
      return false;

   scaled_data = (uint32_t*)malloc(sizeof(uint32_t) * target_size * target_size);
   if (!scaled_data)
   {
      free(icon_data);
      return false;
   }

   sdl_miyoomini_scale_argb(icon_data, icon_width, icon_height,
         scaled_data, target_size, target_size);

   free(icon_data);
   free(vid->cheevos_icon_data);
   free(vid->cheevos_icon_restore_data);
   vid->cheevos_icon_data                = NULL;
   vid->cheevos_icon_width               = 0;
   vid->cheevos_icon_height              = 0;
   vid->cheevos_icon_size                = 0;
   vid->cheevos_icon_background_stored   = false;
   vid->cheevos_icon_restore_pending     = false;
   vid->cheevos_icon_restore_data        = NULL;
   vid->cheevos_icon_restore_pitch       = 0;
   vid->cheevos_icon_restore_width       = 0;
   vid->cheevos_icon_restore_height      = 0;
   vid->cheevos_icon_restore_x           = 0;
   vid->cheevos_icon_restore_y           = 0;
   vid->cheevos_icon_data   = scaled_data;
   vid->cheevos_icon_width  = target_size;
   vid->cheevos_icon_height = target_size;
   vid->cheevos_icon_size   = target_size;
   strlcpy(vid->cheevos_badge_name, badge_name, sizeof(vid->cheevos_badge_name));

   return true;
}

static void sdl_miyoomini_store_cheevos_background(
      sdl_miyoomini_video_t *vid,
      unsigned screen_width,
      unsigned screen_height,
      int icon_x,
      int icon_y)
{
   size_t bytes_per_pixel = (size_t)(vinfo.bits_per_pixel / 8);
   size_t row_bytes       = vid->cheevos_icon_width * bytes_per_pixel;
   size_t buffer_bytes    = row_bytes * vid->cheevos_icon_height;
   uint8_t *screen_buf    = NULL;
   unsigned y;

   if (!vid || vid->cheevos_icon_background_stored
         || !vid->cheevos_icon_data || bytes_per_pixel == 0)
      return;


   if (!vid->cheevos_icon_restore_data
         || vid->cheevos_icon_restore_pitch != row_bytes
         || vid->cheevos_icon_restore_width != vid->cheevos_icon_width
         || vid->cheevos_icon_restore_height != vid->cheevos_icon_height)
   {
      free(vid->cheevos_icon_restore_data);
      vid->cheevos_icon_restore_data = malloc(buffer_bytes);
      if (!vid->cheevos_icon_restore_data)
      {
         vid->cheevos_icon_restore_pitch = 0;
         vid->cheevos_icon_restore_width = 0;
         vid->cheevos_icon_restore_height = 0;
         return;
      }

      vid->cheevos_icon_restore_pitch  = row_bytes;
      vid->cheevos_icon_restore_width  = vid->cheevos_icon_width;
      vid->cheevos_icon_restore_height = vid->cheevos_icon_height;
   }

   if ((unsigned)icon_x + vid->cheevos_icon_width > screen_width
         || (unsigned)icon_y + vid->cheevos_icon_height > screen_height)
      return;

   screen_buf = (uint8_t*)fb_addr + (vinfo.yoffset * screen_width * bytes_per_pixel);
   screen_buf += ((size_t)icon_y * screen_width + (size_t)icon_x) * bytes_per_pixel;

   for (y = 0; y < vid->cheevos_icon_height; y++)
   {
      memcpy((uint8_t*)vid->cheevos_icon_restore_data + y * row_bytes,
            screen_buf + y * screen_width * bytes_per_pixel,
            row_bytes);
   }

   vid->cheevos_icon_restore_x = icon_x;
   vid->cheevos_icon_restore_y = icon_y;
   vid->cheevos_icon_background_stored = true;
}

static void sdl_miyoomini_restore_cheevos_background(
      sdl_miyoomini_video_t *vid,
      unsigned screen_width)
{
   size_t bytes_per_pixel = (size_t)(vinfo.bits_per_pixel / 8);
   size_t row_bytes       = vid->cheevos_icon_restore_pitch;
   uint8_t *screen_buf    = NULL;
   unsigned y;

   if (!vid || !vid->cheevos_icon_restore_data || bytes_per_pixel == 0)
      return;


   screen_buf = (uint8_t*)fb_addr + (vinfo.yoffset * screen_width * bytes_per_pixel);
   screen_buf += ((size_t)vid->cheevos_icon_restore_y * screen_width
         + (size_t)vid->cheevos_icon_restore_x) * bytes_per_pixel;

   for (y = 0; y < vid->cheevos_icon_restore_height; y++)
   {
      memcpy(screen_buf + y * screen_width * bytes_per_pixel,
            (uint8_t*)vid->cheevos_icon_restore_data + y * row_bytes,
            row_bytes);
   }

   vid->cheevos_icon_background_stored = false;
}

static void sdl_miyoomini_draw_cheevos_icon(sdl_miyoomini_video_t *vid,
      unsigned screen_width, unsigned screen_height)
{
   const settings_t *settings = NULL;
   unsigned anchor = 0;
   unsigned padding_x = 0;
   unsigned padding_y = 0;
   int icon_x = 0;
   int icon_y = 0;
   unsigned x, y;
   uint32_t *icon_data = NULL;

   if (!vid || !vid->cheevos_icon_visible || !vid->cheevos_icon_data)
      return;


   settings = config_get_ptr();
   anchor = settings->uints.cheevos_appearance_anchor;

   if (settings->bools.cheevos_appearance_padding_auto)
   {
      padding_x = 10;
      padding_y = 10;
   }
   else
   {
      padding_x = (unsigned)(settings->floats.cheevos_appearance_padding_h * screen_width);
      padding_y = (unsigned)(settings->floats.cheevos_appearance_padding_v * screen_height);
   }

   if (anchor == CHEEVOS_APPEARANCE_ANCHOR_TOPCENTER ||
       anchor == CHEEVOS_APPEARANCE_ANCHOR_BOTTOMCENTER)
      icon_x = (int)((screen_width - vid->cheevos_icon_size) / 2) + (int)padding_x;
   else if (anchor == CHEEVOS_APPEARANCE_ANCHOR_TOPRIGHT ||
            anchor == CHEEVOS_APPEARANCE_ANCHOR_BOTTOMRIGHT)
      icon_x = (int)(screen_width - vid->cheevos_icon_size - padding_x);
   else
      icon_x = (int)padding_x;

   if (anchor == CHEEVOS_APPEARANCE_ANCHOR_BOTTOMLEFT ||
       anchor == CHEEVOS_APPEARANCE_ANCHOR_BOTTOMCENTER ||
       anchor == CHEEVOS_APPEARANCE_ANCHOR_BOTTOMRIGHT)
      icon_y = (int)(screen_height - vid->cheevos_icon_size - padding_y);
   else
      icon_y = (int)padding_y;

   if (icon_x < 0 || icon_y < 0)
      return;

   icon_x = (int)(screen_width - (unsigned)icon_x - vid->cheevos_icon_size);
   icon_y = (int)(screen_height - (unsigned)icon_y - vid->cheevos_icon_size);

   if (icon_x < 0 || icon_y < 0)
      return;
   if ((unsigned)(icon_x + vid->cheevos_icon_size) > screen_width
         || (unsigned)(icon_y + vid->cheevos_icon_size) > screen_height)
      return;

   sdl_miyoomini_store_cheevos_background(vid, screen_width, screen_height, icon_x, icon_y);
   icon_data = vid->cheevos_icon_data;

   if (vinfo.bits_per_pixel == 32)
   {
      uint32_t *screen_buf = (uint32_t*)(fb_addr + (vinfo.yoffset * screen_width * sizeof(uint32_t)));

      for (y = 0; y < vid->cheevos_icon_height; y++)
      {
         unsigned src_y = vid->cheevos_icon_height - 1 - y;

         for (x = 0; x < vid->cheevos_icon_width; x++)
         {
            unsigned src_x = vid->cheevos_icon_width - 1 - x;
            uint32_t src_pixel = icon_data[src_y * vid->cheevos_icon_width + src_x];
            uint8_t alpha      = (uint8_t)(src_pixel >> 24);
            uint32_t *dst_pixel = &screen_buf[(icon_y + y) * screen_width + (icon_x + x)];

            if (alpha == 0)
               continue;

            if (alpha == 255)
               *dst_pixel = src_pixel & 0x00ffffff;
            else
            {
               uint32_t dst        = *dst_pixel;
               uint8_t src_r       = (uint8_t)(src_pixel >> 16);
               uint8_t src_g       = (uint8_t)(src_pixel >> 8);
               uint8_t src_b       = (uint8_t)(src_pixel >> 0);
               uint8_t dst_r       = (uint8_t)(dst >> 16);
               uint8_t dst_g       = (uint8_t)(dst >> 8);
               uint8_t dst_b       = (uint8_t)(dst >> 0);
               uint8_t inv_alpha   = 255 - alpha;

               dst_r = (uint8_t)((src_r * alpha + dst_r * inv_alpha) / 255);
               dst_g = (uint8_t)((src_g * alpha + dst_g * inv_alpha) / 255);
               dst_b = (uint8_t)((src_b * alpha + dst_b * inv_alpha) / 255);
               *dst_pixel = (dst_r << 16) | (dst_g << 8) | dst_b;
            }
         }
      }
   }
   else if (vinfo.bits_per_pixel == 16)
   {
      uint16_t *screen_buf = (uint16_t*)(fb_addr + (vinfo.yoffset * screen_width * sizeof(uint16_t)));

      for (y = 0; y < vid->cheevos_icon_height; y++)
      {
         unsigned src_y = vid->cheevos_icon_height - 1 - y;

         for (x = 0; x < vid->cheevos_icon_width; x++)
         {
            unsigned src_x = vid->cheevos_icon_width - 1 - x;
            uint32_t src_pixel = icon_data[src_y * vid->cheevos_icon_width + src_x];
            uint8_t alpha      = (uint8_t)(src_pixel >> 24);
            uint16_t *dst_pixel = &screen_buf[(icon_y + y) * screen_width + (icon_x + x)];

            if (alpha == 0)
               continue;

            if (alpha == 255)
            {
               uint8_t src_r = (uint8_t)(src_pixel >> 16);
               uint8_t src_g = (uint8_t)(src_pixel >> 8);
               uint8_t src_b = (uint8_t)(src_pixel >> 0);
               *dst_pixel = (uint16_t)(((src_r >> 3) << 11) | ((src_g >> 2) << 5) | (src_b >> 3));
            }
            else
            {
               uint16_t dst      = *dst_pixel;
               uint8_t dst_r     = (uint8_t)(((dst >> 11) & 0x1f) << 3);
               uint8_t dst_g     = (uint8_t)(((dst >> 5) & 0x3f) << 2);
               uint8_t dst_b     = (uint8_t)((dst & 0x1f) << 3);
               uint8_t src_r     = (uint8_t)(src_pixel >> 16);
               uint8_t src_g     = (uint8_t)(src_pixel >> 8);
               uint8_t src_b     = (uint8_t)(src_pixel >> 0);
               uint8_t inv_alpha = 255 - alpha;

               dst_r = (uint8_t)((src_r * alpha + dst_r * inv_alpha) / 255);
               dst_g = (uint8_t)((src_g * alpha + dst_g * inv_alpha) / 255);
               dst_b = (uint8_t)((src_b * alpha + dst_b * inv_alpha) / 255);
               *dst_pixel = (uint16_t)(((dst_r >> 3) << 11) | ((dst_g >> 2) << 5) | (dst_b >> 3));
            }
         }
      }
   }
}
#endif

/* Clear OSD text area, without video_rect, rotate180 */
static void sdl_miyoomini_clear_msgarea(void* buf, unsigned x, unsigned y, unsigned w, unsigned h, unsigned lines) {
   if ( ( x == 0 ) && ( w == SDL_MIYOOMINI_WIDTH  ) && ( y == 0 ) && ( h == SDL_MIYOOMINI_HEIGHT ) ) return;

   uint32_t x0 = SDL_MIYOOMINI_WIDTH - (x + w); /* left margin , right margin = x */
   uint32_t y0 = SDL_MIYOOMINI_HEIGHT - (y + h); /* top margin , bottom margin = y */
   uint32_t sl = x0 * sizeof(uint32_t); /* left buffer size */
   uint32_t sr = x * sizeof(uint32_t); /* right buffer size */
   uint32_t sw = w * sizeof(uint32_t); /* pitch */
   uint32_t ss = SDL_MIYOOMINI_WIDTH * sizeof(uint32_t); /* stride */
   uint32_t vy = OSD_TEXT_Y_MARGIN + 2; /* clear start y offset */
   uint32_t vh = FONT_HEIGHT_STRIDE * 2 * lines - 2; /* clear height */
   uint32_t vh1 = (y0 < vy) ? 0 : (y0 - vy); if (vh1 > vh) vh1 = vh;
   uint32_t vh2 = vh - vh1;
   uint32_t ssl = ss * vh1 + sl;
   uint32_t srl = sr + sl;
   void* ofs = buf + vy * ss;

   if (ssl) memset(ofs, 0, ssl);
   ofs += ssl + sw;
   for (; vh2>1; vh2--, ofs += ss) { if (srl) memset(ofs, 0, srl); }
   if ((vh2) && (sr)) memset(ofs, 0, sr);
}

static INLINE uint32_t sdl_miyoomini_blend_888(uint32_t dst, uint32_t src, uint8_t alpha)
{
   if (alpha == 0)
      return dst;
   if (alpha == 255)
      return src;

   {
      uint8_t inv_alpha = 255 - alpha;
      uint8_t sr        = (src >> 16) & 0xFF;
      uint8_t sg        = (src >> 8)  & 0xFF;
      uint8_t sb        = (src)       & 0xFF;
      uint8_t dr        = (dst >> 16) & 0xFF;
      uint8_t dg        = (dst >> 8)  & 0xFF;
      uint8_t db        = (dst)       & 0xFF;

      dr = (uint8_t)((sr * alpha + dr * inv_alpha) / 255);
      dg = (uint8_t)((sg * alpha + dg * inv_alpha) / 255);
      db = (uint8_t)((sb * alpha + db * inv_alpha) / 255);

      return (dr << 16) | (dg << 8) | db;
   }
}

static void sdl_miyoomini_blend_rect_888(
      uint32_t *screen_buf,
      int screen_width,
      int screen_height,
      int x,
      int y,
      int width,
      int height,
      uint32_t color,
      uint8_t alpha)
{
   int px, py;
   int x_end = x + width;
   int y_end = y + height;

   if (!screen_buf || alpha == 0 || width <= 0 || height <= 0)
      return;

   if (x < 0)
      x = 0;
   if (y < 0)
      y = 0;
   if (x_end > screen_width)
      x_end = screen_width;
   if (y_end > screen_height)
      y_end = screen_height;
   if (x >= x_end || y >= y_end)
      return;

   for (py = y; py < y_end; py++)
   {
      uint32_t *row_ptr = screen_buf + (py * screen_width);
      for (px = x; px < x_end; px++)
         row_ptr[px] = sdl_miyoomini_blend_888(row_ptr[px], color, alpha);
   }
}

/* Print OSD text, flip callback, direct draw to framebuffer, 32bpp, 2x, rotate180 */
static void sdl_miyoomini_print_msg(void* data) {
   if (unlikely(!data)) return;
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   bool msg_bg_enable   = vid->msg_bg_enable_cached;
   uint8_t msg_bg_alpha = vid->msg_bg_alpha_cached;
   uint32_t msg_bg_rgb  = vid->msg_bg_rgb_cached;

   void *screen_buf;
   const char *str  = vid->msg_tmp;
   uint32_t str_len = vid->msg_len_cached;
   if (str_len) {
      screen_buf              = fb_addr + (vinfo.yoffset * SDL_MIYOOMINI_WIDTH * sizeof(uint32_t));
      sdl_miyoomini_init_font_color(vid);
      bool **font_lut         = vid->osd_font->lut;
      uint32_t str_lines      = (uint32_t)((str_len - 1) / OSD_TEXT_LINE_LEN) + 1;
      uint32_t str_counter    = OSD_TEXT_LINE_LEN;
      const int x_pos_def     = SDL_MIYOOMINI_WIDTH - (FONT_WIDTH_STRIDE * 2);
      int x_pos               = x_pos_def;
      int y_pos               = OSD_TEXT_Y_MARGIN - 4 + (FONT_HEIGHT_STRIDE * 2 * str_lines);
      uint32_t line_step      = FONT_HEIGHT_STRIDE * 2;
      uint32_t line_char_w    = FONT_WIDTH_STRIDE * 2;
      uint32_t line_bg_h      = (FONT_HEIGHT * 2) + 4;
      uint32_t line_bg_y_pad  = 2;
      uint32_t line_index;

      if (msg_bg_enable && msg_bg_alpha > 0)
      {
         for (line_index = 0; line_index < vid->msg_line_count_cached; line_index++)
         {
            unsigned chars = vid->msg_line_chars_cached[line_index];
            int line_y     = y_pos - ((int)line_index * (int)line_step);
            int bg_x;
            int bg_y;
            int bg_w;

            if (!chars)
               continue;

            bg_w = ((int)chars * (int)line_char_w) + 2;
            bg_x = x_pos_def - bg_w + 4;
            bg_y = line_y - ((int)line_bg_h - (int)line_bg_y_pad);

            sdl_miyoomini_blend_rect_888(
                  (uint32_t*)screen_buf,
                  SDL_MIYOOMINI_WIDTH,
                  SDL_MIYOOMINI_HEIGHT,
                  bg_x,
                  bg_y,
                  bg_w,
                  (int)line_bg_h,
                  msg_bg_rgb,
                  msg_bg_alpha);
         }
      }

      for (; str_len > 0; str_len--) {
         /* Check for out of bounds x coordinates */
         if (!str_counter--) {
            x_pos = x_pos_def; y_pos -= (FONT_HEIGHT_STRIDE * 2); str_counter = OSD_TEXT_LINE_LEN;
         }
         /* Deal with spaces first, for efficiency */
         if (*str == ' ') str++;
         else {
            uint32_t i, j;
            bool *symbol_lut;
            uint32_t symbol = utf8_walk(&str);

            /* Stupid hack: 'oe' ligatures are not really
             * standard extended ASCII, so we have to waste
             * CPU cycles performing a conversion from the
             * unicode values... */
            if (symbol == 339) /* Latin small ligature oe */
               symbol = 156;
            if (symbol == 338) /* Latin capital ligature oe */
               symbol = 140;

            if (symbol >= SDL_NUM_FONT_GLYPHS) continue;

            symbol_lut = font_lut[symbol];

            for (j = 0; j < FONT_HEIGHT; j++) {
               uint32_t buff_offset = ((y_pos - (j * 2) ) * SDL_MIYOOMINI_WIDTH) + x_pos;

               for (i = 0; i < FONT_WIDTH; i++) {
                  if (*(symbol_lut + i + (j * FONT_WIDTH))) {
                     uint32_t *screen_buf_ptr = (uint32_t*)screen_buf + buff_offset - (i * 2);

                     /* Bottom shadow (1) */
                     if (msg_bg_enable)
                     {
                        screen_buf_ptr[+0] = sdl_miyoomini_blend_888(screen_buf_ptr[+0], msg_bg_rgb, msg_bg_alpha);
                        screen_buf_ptr[+1] = sdl_miyoomini_blend_888(screen_buf_ptr[+1], msg_bg_rgb, msg_bg_alpha);
                        screen_buf_ptr[+2] = sdl_miyoomini_blend_888(screen_buf_ptr[+2], msg_bg_rgb, msg_bg_alpha);
                        screen_buf_ptr[+3] = sdl_miyoomini_blend_888(screen_buf_ptr[+3], msg_bg_rgb, msg_bg_alpha);
                     }
                     else
                     {
                        screen_buf_ptr[+0] = 0;
                        screen_buf_ptr[+1] = 0;
                        screen_buf_ptr[+2] = 0;
                        screen_buf_ptr[+3] = 0;
                     }

                     /* Bottom shadow (2) */
                     if (msg_bg_enable)
                     {
                        screen_buf_ptr[SDL_MIYOOMINI_WIDTH+0] = sdl_miyoomini_blend_888(screen_buf_ptr[SDL_MIYOOMINI_WIDTH+0], msg_bg_rgb, msg_bg_alpha);
                        screen_buf_ptr[SDL_MIYOOMINI_WIDTH+1] = sdl_miyoomini_blend_888(screen_buf_ptr[SDL_MIYOOMINI_WIDTH+1], msg_bg_rgb, msg_bg_alpha);
                        screen_buf_ptr[SDL_MIYOOMINI_WIDTH+2] = sdl_miyoomini_blend_888(screen_buf_ptr[SDL_MIYOOMINI_WIDTH+2], msg_bg_rgb, msg_bg_alpha);
                        screen_buf_ptr[SDL_MIYOOMINI_WIDTH+3] = sdl_miyoomini_blend_888(screen_buf_ptr[SDL_MIYOOMINI_WIDTH+3], msg_bg_rgb, msg_bg_alpha);
                     }
                     else
                     {
                        screen_buf_ptr[SDL_MIYOOMINI_WIDTH+0] = 0;
                        screen_buf_ptr[SDL_MIYOOMINI_WIDTH+1] = 0;
                        screen_buf_ptr[SDL_MIYOOMINI_WIDTH+2] = 0;
                        screen_buf_ptr[SDL_MIYOOMINI_WIDTH+3] = 0;
                     }

                     /* Text pixel + right shadow (1) */
                     if (msg_bg_enable)
                     {
                        screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*2)+0] = sdl_miyoomini_blend_888(screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*2)+0], msg_bg_rgb, msg_bg_alpha);
                        screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*2)+1] = sdl_miyoomini_blend_888(screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*2)+1], msg_bg_rgb, msg_bg_alpha);
                     }
                     else
                     {
                        screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*2)+0] = 0;
                        screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*2)+1] = 0;
                     }
                     screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*2)+2] = vid->font_colour32;
                     screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*2)+3] = vid->font_colour32;

                     /* Text pixel + right shadow (2) */
                     if (msg_bg_enable)
                     {
                        screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*3)+0] = sdl_miyoomini_blend_888(screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*3)+0], msg_bg_rgb, msg_bg_alpha);
                        screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*3)+1] = sdl_miyoomini_blend_888(screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*3)+1], msg_bg_rgb, msg_bg_alpha);
                     }
                     else
                     {
                        screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*3)+0] = 0;
                        screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*3)+1] = 0;
                     }
                     screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*3)+2] = vid->font_colour32;
                     screen_buf_ptr[(SDL_MIYOOMINI_WIDTH*3)+3] = vid->font_colour32;
                  }
               }
            }
         }
         x_pos -= FONT_WIDTH_STRIDE * 2;
      }
      vid->msg_count |= (str_lines << 6);
   }
   if (vid->msg_count & 7) {
      /* clear recent OSD text */
      screen_buf = fb_addr;
      uint32_t target_offset = vinfo.yoffset + SDL_MIYOOMINI_HEIGHT;
      if (target_offset != SDL_MIYOOMINI_HEIGHT * 3) screen_buf += target_offset * SDL_MIYOOMINI_WIDTH * sizeof(uint32_t);
      sdl_miyoomini_clear_msgarea(screen_buf, vid->video_x, vid->video_y, vid->video_w, vid->video_h, vid->msg_count & 7);
   }
   vid->msg_count >>= 3;
#ifdef HAVE_CHEEVOS
   sdl_miyoomini_cheevos_lock(vid);
   if (vid->cheevos_icon_timer > 0
         || vid->cheevos_icon_visible
         || vid->cheevos_icon_restore_pending)
   if (vid->cheevos_icon_timer > 0 && vid->cheevos_icon_visible)
   {
      sdl_miyoomini_draw_cheevos_icon(vid, SDL_MIYOOMINI_WIDTH, SDL_MIYOOMINI_HEIGHT);
      vid->cheevos_icon_timer--;
      if (vid->cheevos_icon_timer == 0)
         vid->cheevos_icon_restore_pending = true;
   }

   if (vid->cheevos_icon_restore_pending)
   {
      sdl_miyoomini_restore_cheevos_background(vid, SDL_MIYOOMINI_WIDTH);
      vid->cheevos_icon_restore_pending = false;
   }
   sdl_miyoomini_cheevos_unlock(vid);
#endif
}

/* Nearest neighbor scalers */
#define NN_SHIFT 16
void scalenn_16(void* data, void* __restrict src, void* __restrict dst, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dp) {
   if (unlikely(!data||!sw||!sh)) return;
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   uint32_t dw = vid->video_w;
   uint32_t dh = vid->video_h;

   uint32_t x_step = (sw << NN_SHIFT) / dw + 1;
   uint32_t y_step = (sh << NN_SHIFT) / dh + 1;

   uint32_t in_stride  = sp >> 1;
   uint32_t out_stride = dp >> 1;

   uint16_t* in_ptr  = (uint16_t*)src;
   uint16_t* out_ptr = (uint16_t*)dst;

   uint32_t oy = 0;
   uint32_t y  = 0;

   /* Reading 16bits takes a little time,
      so try not to read as much as possible in the case of 16bpp */
   if (dh > sh) {
      if (dw > sw) {
         do {
            uint32_t col = dw;
            uint32_t ox  = 0;
            uint32_t x   = 0;

            uint16_t* optrtmp1 = out_ptr;

            uint16_t pix = in_ptr[0];
            do {
               uint32_t tx = x >> NN_SHIFT;
               if (tx != ox) {
                  pix = in_ptr[tx];
                  ox  = tx;
               }
               *(out_ptr++) = pix;
               x           += x_step;
            } while (--col);

            y += y_step;
            uint32_t ty = y >> NN_SHIFT;
            uint16_t* optrtmp2 = optrtmp1;
            for(; ty == oy; y += y_step, ty = y >> NN_SHIFT) {
               if (!--dh) return;
               optrtmp2 += out_stride;
               memcpy(optrtmp2, optrtmp1, dw << 1);
            }
            in_ptr += (ty - oy) * in_stride;
            out_ptr = optrtmp2 + out_stride;
            oy      = ty;
         } while (--dh);
      } else {
         do {
            uint32_t col = dw;
            uint32_t x   = 0;

            uint16_t* optrtmp1 = out_ptr;

            do {
               *(out_ptr++) = in_ptr[x >> NN_SHIFT];
               x           += x_step;
            } while (--col);

            y += y_step;
            uint32_t ty = y >> NN_SHIFT;
            uint16_t* optrtmp2 = optrtmp1;
            for(; ty == oy; y += y_step, ty = y >> NN_SHIFT) {
               if (!--dh) return;
               optrtmp2 += out_stride;
               memcpy(optrtmp2, optrtmp1, dw << 1);
            }
            in_ptr += (ty - oy) * in_stride;
            out_ptr = optrtmp2 + out_stride;
            oy      = ty;
         } while (--dh);
      }
   } else {
      if (dw > sw) {
         do {
            uint32_t col = dw;
            uint32_t ox  = 0;
            uint32_t x   = 0;

            uint16_t* optrtmp1 = out_ptr;

            uint16_t pix = in_ptr[0];
            do {
               uint32_t tx = x >> NN_SHIFT;
               if (tx != ox) {
                  pix = in_ptr[tx];
                  ox  = tx;
               }
               *(out_ptr++) = pix;
               x           += x_step;
            } while (--col);

            y += y_step;
            uint32_t ty = y >> NN_SHIFT;
            in_ptr += (ty - oy) * in_stride;
            out_ptr = optrtmp1 + out_stride;
            oy      = ty;
         } while (--dh);
      } else {
         do {
            uint32_t col = dw;
            uint32_t x   = 0;

            uint16_t* optrtmp1 = out_ptr;

            do {
               *(out_ptr++) = in_ptr[x >> NN_SHIFT];
               x           += x_step;
            } while (--col);

            y += y_step;
            uint32_t ty = y >> NN_SHIFT;
            in_ptr += (ty - oy) * in_stride;
            out_ptr = optrtmp1 + out_stride;
            oy      = ty;
         } while (--dh);
      }
   }
}

void scalenn_32(void* data, void* __restrict src, void* __restrict dst, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dp) {
   if (unlikely(!data||!sw||!sh)) return;
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   uint32_t dw = vid->video_w;
   uint32_t dh = vid->video_h;

   uint32_t x_step = (sw << NN_SHIFT) / dw + 1;
   uint32_t y_step = (sh << NN_SHIFT) / dh + 1;

   uint32_t in_stride  = sp >> 2;
   uint32_t out_stride = dp >> 2;

   uint32_t* in_ptr  = (uint32_t*)src;
   uint32_t* out_ptr = (uint32_t*)dst;

   uint32_t oy = 0;
   uint32_t y  = 0;

   /* Reading 32bit is fast when cached,
      so the x-axis is not considered in the case of 32bpp */
   if (dh > sh) {
      do {
         uint32_t col = dw;
         uint32_t x   = 0;

         uint32_t* optrtmp1 = out_ptr;

         do {
            *(out_ptr++) = in_ptr[x >> NN_SHIFT];
            x           += x_step;
         } while (--col);

         y += y_step;
         uint32_t ty = y >> NN_SHIFT;
         uint32_t* optrtmp2 = optrtmp1;
         for(; ty == oy; y += y_step, ty = y >> NN_SHIFT) {
            if (!--dh) return;
            optrtmp2 += out_stride;
            memcpy(optrtmp2, optrtmp1, dw << 2);
         }
         in_ptr += (ty - oy) * in_stride;
         out_ptr = optrtmp2 + out_stride;
         oy      = ty;
      } while (--dh);
   } else {
      do {
         uint32_t col = dw;
         uint32_t x   = 0;

         uint32_t* optrtmp1 = out_ptr;

         do {
            *(out_ptr++) = in_ptr[x >> NN_SHIFT];
            x           += x_step;
         } while (--col);

         y += y_step;
         uint32_t ty = y >> NN_SHIFT;
         in_ptr += (ty - oy) * in_stride;
         out_ptr = optrtmp1 + out_stride;
         oy      = ty;
      } while (--dh);
   }
}

/* Clear border x3 screens for framebuffer (rotate180) */
static void sdl_miyoomini_clear_border(void* buf, unsigned x, unsigned y, unsigned w, unsigned h) {
   if ( (x == 0) && (y == 0) && (w == SDL_MIYOOMINI_WIDTH) && (h == SDL_MIYOOMINI_HEIGHT) ) return;
   if ( (w == 0) || (h == 0) ) { memset(buf, 0, SDL_MIYOOMINI_WIDTH * SDL_MIYOOMINI_HEIGHT * sizeof(uint32_t) * 3); return; }

   uint32_t x0 = SDL_MIYOOMINI_WIDTH - (x + w); /* left margin , right margin = x */
   uint32_t y0 = SDL_MIYOOMINI_HEIGHT - (y + h); /* top margin , bottom margin = y */
   uint32_t sl = x0 * sizeof(uint32_t); /* left buffer size */
   uint32_t sr = x * sizeof(uint32_t); /* right buffer size */
   uint32_t st = y0 * SDL_MIYOOMINI_WIDTH * sizeof(uint32_t); /* top buffer size */
   uint32_t sb = y * SDL_MIYOOMINI_WIDTH * sizeof(uint32_t); /* bottom buffer size */
   uint32_t srl = sr + sl;
   uint32_t stl = st + sl;
   uint32_t srb = sr + sb;
   uint32_t srbtl = srl + sb + st;
   uint32_t sw = w * sizeof(uint32_t); /* pitch */
   uint32_t ss = SDL_MIYOOMINI_WIDTH * sizeof(uint32_t); /* stride */
   uint32_t i;

   if (stl) memset(buf, 0, stl); /* 1st top + 1st left */
   buf += stl + sw;
   for (i=h-1; i>0; i--, buf += ss) {
      if (srl) memset(buf, 0, srl); /* right + left */
   }
   if (srbtl) memset(buf, 0, srbtl); /* last right + bottom + top + 1st left */
   buf += srbtl + sw;
   for (i=h-1; i>0; i--, buf += ss) {
      if (srl) memset(buf, 0, srl); /* right + left */
   }
   if (srbtl) memset(buf, 0, srbtl); /* last right + bottom + top + 1st left */
   buf += srbtl + sw;
   for (i=h-1; i>0; i--, buf += ss) {
      if (srl) memset(buf, 0, srl); /* right + left */
   }
   if (srb) memset(buf, 0, srb); /* last right + last bottom */
}

/* Set cpuclock */
#define	BASE_REG_RIU_PA		(0x1F000000)
#define	BASE_REG_MPLL_PA	(BASE_REG_RIU_PA + 0x103000*2)
#define	PLL_SIZE		(0x1000)
static void set_cpuclock(int clock) {
	sync();
	int fd_mem = open("/dev/mem", O_RDWR);
	void* pll_map = mmap(0, PLL_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd_mem, BASE_REG_MPLL_PA);

	uint32_t post_div;
	if (clock >= 800000) post_div = 2;
	else if (clock >= 400000) post_div = 4;
	else if (clock >= 200000) post_div = 8;
	else post_div = 16;

	static const uint64_t divsrc = 432000000llu * 524288;
	uint32_t rate = (clock * 1000)/16 * post_div / 2;
	uint32_t lpf = (uint32_t)(divsrc / rate);
	volatile uint16_t* p16 = (uint16_t*)pll_map;

	uint32_t cur_post_div = (p16[0x232] & 0x0F) + 1;
	uint32_t tmp_post_div = cur_post_div;
	if (post_div > cur_post_div) {
		while (tmp_post_div != post_div) {
			tmp_post_div <<= 1;
			p16[0x232] = (p16[0x232] & 0xF0) | ((tmp_post_div-1) & 0x0F);
		}
	}

	p16[0x2A8] = 0x0000;
	p16[0x2AE] = 0x000F;
	p16[0x2A4] = lpf&0xFFFF;
	p16[0x2A6] = lpf>>16;
	p16[0x2B0] = 0x0001;
	p16[0x2B2] |= 0x1000;
	p16[0x2A8] = 0x0001;
	while( !(p16[0x2BA]&1) );
	p16[0x2A0] = lpf&0xFFFF;
	p16[0x2A2] = lpf>>16;

	if (post_div < cur_post_div) {
		while (tmp_post_div != post_div) {
			tmp_post_div >>= 1;
			p16[0x232] = (p16[0x232] & 0xF0) | ((tmp_post_div-1) & 0x0F);
		}
	}

	munmap(pll_map, PLL_SIZE);
	close(fd_mem);
}

int miyoo_gfx_apply_cpuclock(int clock)
{
   const char *governor_paths[] = {
      "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor",
      "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
   };
   const char *setspeed_paths[] = {
      "/sys/devices/system/cpu/cpufreq/policy0/scaling_setspeed",
      "/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed"
   };
   char str[16];
   bool wrote_any           = false;
   size_t i;

   if (clock < 200000)
      clock = 200000;
   else if (clock > 1600000)
      clock = 1600000;

   for (i = 0; i < (sizeof(governor_paths) / sizeof(governor_paths[0])); i++)
   {
      FILE *fp = fopen(governor_paths[i], "w");
      if (!fp)
         continue;
      fwrite("userspace", 1, strlen("userspace"), fp);
      fclose(fp);
      wrote_any = true;
   }

   snprintf(str, sizeof(str), "%d", clock);
   for (i = 0; i < (sizeof(setspeed_paths) / sizeof(setspeed_paths[0])); i++)
   {
      int fset = open(setspeed_paths[i], O_WRONLY);
      if (fset < 0)
         continue;
      write(fset, str, strlen(str));
      close(fset);
      wrote_any = true;
   }

   sync();
   set_cpuclock(clock);
   return wrote_any ? 0 : -1;
}

static void print_clock(void) {
	int fd_mem = open("/dev/mem", O_RDWR);
	void* pll_map = mmap(0, PLL_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd_mem, BASE_REG_MPLL_PA);
	uint32_t		rate;
	uint32_t		lpf_value;
	uint32_t		post_div;
	volatile uint8_t*	p8  = (uint8_t*)pll_map;
	volatile uint16_t*	p16 = (uint16_t*)pll_map;

	//get LPF / post_div
	lpf_value = p16[0x2A4] + (p16[0x2A6] << 16); post_div = p16[0x232] + 1;
	if (lpf_value == 0) lpf_value= (p8[0x2C2<<1] <<  16) + (p8[0x2C1<<1] << 8) + p8[0x2C0<<1];

	/*
	 * Calculate LPF value for DFS
	 * LPF_value(5.19) = (432MHz / Ref_clk) * 2^19  =>  it's for post_div=2
	 * Ref_clk = CPU_CLK * 2 / 32
	 */
	static const uint64_t divsrc = 432000000llu * 524288;
	rate = (divsrc / lpf_value * 2 / post_div * 16);

	RARCH_LOG("[CPU]: clock=%u (lpf=%u, post_div=%u)\n", rate, lpf_value, post_div);
	close(fd_mem);
}

/* Set CPU governor */
enum cpugov { PERFORMANCE = 0, POWERSAVE = 1, ONDEMAND = 2, USERSPACE = 3 };
static void sdl_miyoomini_set_cpugovernor(enum cpugov gov) {
   const char govstr[4][12] = { "performance", "powersave", "ondemand", "userspace" };
   const char fn_min_freq[] = "/sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq";
   const char fn_governor[] = "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor";
   const char fn_setspeed[] = "/sys/devices/system/cpu/cpufreq/policy0/scaling_setspeed";
   static uint32_t minfreq = 0;
   FILE* fp;
   FILE *fps = NULL;
   char config_directory[PATH_MAX_LENGTH];
   char cpuclock_config_path[PATH_MAX_LENGTH];
   char rom_cpuclock_config_path[PATH_MAX_LENGTH];
   char rom_cpu_file[PATH_MAX_LENGTH];
   const char *clock_path_used = NULL;
   rarch_system_info_t *system = &runloop_state_get_ptr()->system;
   const char *core_name = system ? system->info.library_name : NULL;
   const char *rarch_path_basename = path_get(RARCH_PATH_BASENAME);
   const char *rom_name = path_basename_nocompression(rarch_path_basename);

   if (!minfreq) {
      /* save min_freq */
      fp = fopen(fn_min_freq, "r");
      if (fp) { fscanf(fp, "%d", &minfreq); fclose(fp); }
      /* set min_freq to lowest */
      fp = fopen(fn_min_freq, "w");
      if (fp) { fprintf(fp, "%d", 0); fclose(fp); }
   }

   if (gov == ONDEMAND) {
      /* revert min_freq */
      fp = fopen(fn_min_freq, "w");
      if (fp) { fprintf(fp, "%d", minfreq); fclose(fp); }
      minfreq = 0;
   }

   /* set cpu clock to value in cpuclock.txt */
   if (gov == PERFORMANCE) {
#if defined(MIYOO_CUSTOM_MENU)
      {
         long runtime_clock = 0;
         if (miyoo_menu_cpu_clock_get_runtime_override(&runtime_clock)
               && runtime_clock >= 200000
               && runtime_clock <= 1600000)
         {
            char str[16];
            fp = fopen(fn_governor, "w");
            if (fp) { fwrite(govstr[USERSPACE], 1, strlen(govstr[USERSPACE]), fp); fclose(fp); }
            int fset = open(fn_setspeed, O_WRONLY);
            snprintf(str, sizeof(str), "%ld", runtime_clock);
            if (fset >= 0) { write(fset, str, strlen(str)); close(fset); }
            set_cpuclock((int)runtime_clock);
            RARCH_LOG("[CPU]: Runtime override clock: %ld MHz\n", runtime_clock / 1000);
            print_clock();
            return;
         }
      }
#endif
	   
	  if (!string_is_empty(core_name)) {
      /* Get base config directory */
      fill_pathname_application_special(config_directory, sizeof(config_directory), APPLICATION_SPECIAL_DIRECTORY_CONFIG);

      if (!string_is_empty(rom_name))
      {
         snprintf(rom_cpu_file, sizeof(rom_cpu_file), "%s-cpu", rom_name);
         fill_pathname_join_special_ext(rom_cpuclock_config_path, config_directory, core_name, rom_cpu_file, ".txt", PATH_MAX_LENGTH);
         fps = fopen(rom_cpuclock_config_path, "r");
         clock_path_used = rom_cpuclock_config_path;
         RARCH_LOG("[CPU]: ROM path %s: %s\n", fps ? "found" : "not found", rom_cpuclock_config_path);
      }

      if (!fps)
      {
         /* Get core config path for cpuclock.txt */
         fill_pathname_join_special_ext(cpuclock_config_path, config_directory, core_name, "cpuclock", ".txt", PATH_MAX_LENGTH);
         fps = fopen(cpuclock_config_path, "r");
         clock_path_used = cpuclock_config_path;
      }
      }
	   
      if (fps) {
         int cpuclock = 0;
		 char str[16];
		 RARCH_LOG("[CPU]: Path %s: %s\n", fps ? "found" : "not found", clock_path_used ? clock_path_used : "unknown");
         fscanf(fps, "%d", &cpuclock); fclose(fps);
         if ((cpuclock >= 200000)&&(cpuclock <= 1600000)) {
            fp = fopen(fn_governor, "w");
            if (fp) { fwrite(govstr[USERSPACE], 1, strlen(govstr[USERSPACE]), fp); fclose(fp); }
            int fset = open(fn_setspeed, O_WRONLY);
			sprintf(str, "%d", cpuclock);
            if (fset>=0) { write(fset, str, strlen(str)); close(fset); }
            set_cpuclock(cpuclock);
            RARCH_LOG("[CPU]: Set clock: %d MHz\n", cpuclock / 1000);
			print_clock();
            return;
         } else {
			char *governor = "performance";
			RARCH_LOG("[CPU]: invalid cpu config value\n");
	        fps = fopen("/mnt/SDCARD/.simplemenu/cpu.sav", "r");
			RARCH_LOG("[CPU]: Path %s: ./cpu.sav\n", fps ? "found" : "not found");
			if (fps)
			   fclose(fps);
			fp = fopen(fn_governor, "w");
            if (fp) { fprintf(fp, "%s", governor); fclose(fp); }
			return;
      }
   }
	   
      if (!fps) {
		 int cpuclock = 0;
		 char *governor = "performance";
		 fps = fopen("/mnt/SDCARD/.simplemenu/cpu.sav", "r");
         RARCH_LOG("[CPU]: Path %s: ./cpu.sav\n", fps ? "found" : "not found");
		 if (fps) {
		    fscanf(fps, "%d", &cpuclock);
		    fclose(fps);
         }
		 if ((cpuclock >= 200000)&&(cpuclock <= 1600000)) {
			fp = fopen(fn_governor, "w");
            if (fp) { fprintf(fp, "%s", governor); fclose(fp); }
            RARCH_LOG("[CPU]: Clock is: %d MHz\n", cpuclock / 1000);
			return;
		 }
	  }
   }

   /* set governor */
   fp = fopen(fn_governor, "w");
   if (fp) { fwrite(govstr[gov], 1, strlen(govstr[gov]), fp); fclose(fp); }
}

static void sdl_miyoomini_toggle_powersave(bool state) {
   sdl_miyoomini_set_cpugovernor(state ? ONDEMAND: PERFORMANCE);
}

static void sdl_miyoomini_sighandler(int sig) {
   switch (sig) {
   case SIGSTOP:
      sdl_miyoomini_toggle_powersave(true);
      break;
   case SIGCONT:
      sdl_miyoomini_toggle_powersave(false);
      break;
   default:
      break;
   }
}

static void sdl_miyoomini_init_font_color(sdl_miyoomini_video_t *vid) {
   settings_t *settings = config_get_ptr();
   uint32_t red         = 0xFF;
   uint32_t green       = 0xFF;
   uint32_t blue        = 0xFF;

   if (settings) {
      red   = (uint32_t)((settings->floats.video_msg_color_r * 255.0f) + 0.5f) & 0xFF;
      green = (uint32_t)((settings->floats.video_msg_color_g * 255.0f) + 0.5f) & 0xFF;
      blue  = (uint32_t)((settings->floats.video_msg_color_b * 255.0f) + 0.5f) & 0xFF;
   }

   /* Convert to XRGB8888 */
   vid->font_colour32 = (red << 16) | (green << 8) | blue;
}

static void sdl_miyoomini_gfx_free(void *data) {
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   if (unlikely(!vid)) return;

   if (GFX_GetFlipCallback()) {
      GFX_SetFlipCallback(NULL, NULL); usleep(0x2000); /* wait for finish callback */
   }
   GFX_WaitAllDone();
   for (unsigned i = 0; i < 2; i++) {
      if (vid->screen_fence[i]) {
         MI_GFX_WaitAllDone(FALSE, vid->screen_fence[i]);
         vid->screen_fence[i] = 0;
      }
      if (vid->screens[i]) {
         GFX_FreeSurface(vid->screens[i]);
         vid->screens[i] = NULL;
      }
   }
   vid->screen = NULL;
   if (vid->menuscreen) GFX_FreeSurface(vid->menuscreen);
   if (vid->menuscreen_rgui) GFX_FreeSurface(vid->menuscreen_rgui);
#ifdef HAVE_OVERLAY
   if (vid->overlay_surface) { GFX_SetupOverlaySurface(NULL); GFX_FreeSurface(vid->overlay_surface); }
#endif
   GFX_Quit();

   if (vid->osd_font) bitmapfont_free_lut(vid->osd_font);

#ifdef HAVE_CHEEVOS
   sdl_miyoomini_free_cheevos_icon(vid);
   if (vid->cheevos_lock)
   {
      SDL_DestroyMutex(vid->cheevos_lock);
      vid->cheevos_lock = NULL;
   }
#endif

   free(vid);
	
   {
      int restore_clock = 0;
      FILE *fps = fopen("/mnt/SDCARD/.simplemenu/cpu.sav", "r");
      if (fps) {
         fscanf(fps, "%d", &restore_clock);
         fclose(fps);
      }
      if (restore_clock >= 200000 && restore_clock <= 1200000) {
         char str[32];
         const char *clockpaths[] = {
            "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq",
            "/sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq"
         };
         snprintf(str, sizeof(str), "%d", restore_clock);
         for (size_t i = 0; i < sizeof(clockpaths)/sizeof(clockpaths[0]); i++) {
            FILE *fp = fopen(clockpaths[i], "w");
            if (fp) { fputs(str, fp); fclose(fp); }
         }
         RARCH_LOG("[CPU]: Exit restore scaling_max_freq to cpu.sav: %d MHz\n",
                   restore_clock / 1000);
      }
   }

   sdl_miyoomini_set_cpugovernor(ONDEMAND);
}

static void sdl_miyoomini_input_driver_init(
      const char *input_drv_name, const char *joypad_drv_name,
      input_driver_t **input, void **input_data) {
   /* Sanity check */
   if (!input || !input_data) return;

   *input      = NULL;
   *input_data = NULL;

   /* If input driver name is empty, cannot
    * initialise anything... */
   if (string_is_empty(input_drv_name)) return;

   signal(SIGSTOP, sdl_miyoomini_sighandler);
   signal(SIGCONT, sdl_miyoomini_sighandler);

   if (string_is_equal(input_drv_name, "sdl2")) {
      *input_data = input_driver_init_wrap(&input_sdl,
            joypad_drv_name);
      if (*input_data) *input = &input_sdl;
      return;
   }

#if defined(MIYOOMINI)
   if (string_is_equal(input_drv_name, "sdl_dingux")) {
      *input_data = input_driver_init_wrap(&input_sdl_dingux,
            joypad_drv_name);
      if (*input_data) *input = &input_sdl_dingux;
      return;
   }
#endif

#if defined(HAVE_SDL) || defined(HAVE_SDL2)
   if (string_is_equal(input_drv_name, "sdl")) {
      *input_data = input_driver_init_wrap(&input_sdl,
            joypad_drv_name);
      if (*input_data) *input = &input_sdl;
      return;
   }
#endif

#if defined(HAVE_UDEV)
   if (string_is_equal(input_drv_name, "udev")) {
      *input_data = input_driver_init_wrap(&input_udev,
            joypad_drv_name);
      if (*input_data) *input = &input_udev;
      return;
   }
#endif

#if defined(__linux__)
   if (string_is_equal(input_drv_name, "linuxraw")) {
      *input_data = input_driver_init_wrap(&input_linuxraw,
            joypad_drv_name);
      if (*input_data) *input = &input_linuxraw;
      return;
   }
#endif
}

static void sdl_miyoomini_set_output(sdl_miyoomini_video_t* vid, unsigned width, unsigned height, bool rgb32) {
   unsigned old_video_x      = vid->video_x;
   unsigned old_video_y      = vid->video_y;
   unsigned old_video_w      = vid->video_w;
   unsigned old_video_h      = vid->video_h;
   unsigned old_frame_width  = vid->frame_width;
   unsigned old_frame_height = vid->frame_height;
   SDL_Surface *ref_screen   = vid->screen ? vid->screen :
         (vid->screens[0] ? vid->screens[0] : vid->screens[1]);
   unsigned old_bpp          = ref_screen ? ref_screen->format->BitsPerPixel : 0;

   vid->content_width  = width;
   vid->content_height = height;
   if (vid->rotate & 1) { width = vid->content_height; height = vid->content_width; }

   /* Calculate scaling factor */
   uint32_t xmul = (SDL_MIYOOMINI_WIDTH<<16) / width;
   uint32_t ymul = (SDL_MIYOOMINI_HEIGHT<<16) / height;
   uint32_t mul_int = (xmul < ymul ? xmul : ymul)>>16;

   /* Change to aspect/fullscreen scaler when integer & screen size is over (no crop) */
   if (vid->scale_integer && mul_int) {
      /* Integer Scaling */
      vid->video_w = width * mul_int;
      vid->video_h = height * mul_int;
      if (!vid->keep_aspect) {
         /* Integer + Fullscreen , keep 4:3 for CRT console emulators */
         uint32_t Wx3 = vid->video_w * 3;
         uint32_t Hx4 = vid->video_h * 4;
         if (Wx3 != Hx4) {
            if (Wx3 > Hx4) vid->video_h = Wx3 / 4;
            else           vid->video_w = Hx4 / 3;
         }
      }
      vid->video_x = (SDL_MIYOOMINI_WIDTH - vid->video_w) >> 1;
      vid->video_y = (SDL_MIYOOMINI_HEIGHT - vid->video_h) >> 1;
   } else if (vid->keep_aspect) {
      /* Aspect Scaling */
      if (xmul > ymul) {
         vid->video_w  = (width * SDL_MIYOOMINI_HEIGHT) / height;
         vid->video_h = SDL_MIYOOMINI_HEIGHT;
         vid->video_x = (SDL_MIYOOMINI_WIDTH - vid->video_w) >> 1;
         vid->video_y = 0;
      } else {
         vid->video_w  = SDL_MIYOOMINI_WIDTH;
         vid->video_h = (height * SDL_MIYOOMINI_WIDTH) / width;
         vid->video_x = 0;
         vid->video_y = (SDL_MIYOOMINI_HEIGHT - vid->video_h) >> 1;
      }
   } else {
      /* Fullscreen */
      vid->video_w = SDL_MIYOOMINI_WIDTH;
      vid->video_h = SDL_MIYOOMINI_HEIGHT;
      vid->video_x = 0;
      vid->video_y = 0;
   }

   /* align to x4 bytes */
   if (!rgb32) { vid->video_x &= ~1; vid->video_w &= ~1; }

   /* Select scaler to use */
   uint32_t scale_xmul = 0, scale_ymul = 0;
   if ( (vid->filter_type != DINGUX_IPU_FILTER_NEAREST) || (vid->scale_integer && mul_int && vid->keep_aspect) ) {
      scale_xmul = scale_ymul = 1;
      if ( (vid->scale_integer) || (vid->filter_type == DINGUX_IPU_FILTER_BICUBIC) ) {
         // to be at least 80% of the post-scaling size
         scale_xmul = ((vid->video_w<<2)/5 / width) +1;
         scale_ymul = ((vid->video_h<<2)/5 / height) +1;
         if ((scale_xmul == 3)||(scale_xmul > 4)) scale_xmul = 4; // 4x scaler is faster than 3x
         if (scale_ymul > 4) scale_ymul = 4;
      }
   }

   unsigned new_frame_width  = scale_xmul ? vid->content_width  * scale_xmul : vid->video_w;
   unsigned new_frame_height = scale_ymul ? vid->content_height * scale_ymul : vid->video_h;

   vid->frame_width  = new_frame_width;
   vid->frame_height = new_frame_height;

   static void (* const func[2][3][4])(void*, void* __restrict, void* __restrict, uint32_t, uint32_t, uint32_t, uint32_t) = {
      { { &scale1x1_16, &scale1x2_16, &scale1x3_16, &scale1x4_16 },
        { &scale2x1_16, &scale2x2_16, &scale2x3_16, &scale2x4_16 },
        { &scale4x1_16, &scale4x2_16, &scale4x3_16, &scale4x4_16 } },
      { { &scale1x1_32, &scale1x2_32, &scale1x3_32, &scale1x4_32 },
        { &scale2x1_32, &scale2x2_32, &scale2x3_32, &scale2x4_32 },
        { &scale4x1_32, &scale4x2_32, &scale4x3_32, &scale4x4_32 } }
   };

   if (!scale_xmul) {
      vid->scale_func = rgb32 ? scalenn_32 : scalenn_16;
   } else {
      vid->scale_func = func[rgb32?1:0][(scale_xmul>2)?2:scale_xmul-1][scale_ymul-1];
   }

   //RARCH_LOG("[SCALE] cw:%d ch:%d fw:%d fh:%d x:%d y:%d w:%d h:%d xmul:%d ymul:%d\n",vid->content_width,vid->content_height,
   //   vid->frame_width,vid->frame_height,vid->video_x,vid->video_y,vid->video_w,vid->video_h,scale_xmul,scale_ymul);
   bool viewport_changed = (vid->video_x != old_video_x) ||
                          (vid->video_y != old_video_y) ||
                          (vid->video_w != old_video_w) ||
                          (vid->video_h != old_video_h);

   bool surface_size_changed   = (vid->frame_width  != old_frame_width) ||
                                (vid->frame_height != old_frame_height);
   bool surface_format_changed = ((rgb32 ? 32u : 16u) != old_bpp);

   if (!vid->screens[0] || !vid->screens[1] || surface_size_changed || surface_format_changed) {
      bool ok = true;
      GFX_WaitAllDone();
      for (unsigned i = 0; i < 2; i++) {
         if (vid->screen_fence[i]) {
            MI_GFX_WaitAllDone(FALSE, vid->screen_fence[i]);
            vid->screen_fence[i] = 0;
         }
         if (vid->screens[i]) {
            GFX_FreeSurface(vid->screens[i]);
            vid->screens[i] = NULL;
         }
      }
      for (unsigned i = 0; i < 2; i++) {
         vid->screens[i] = GFX_CreateRGBSurface(
               0, vid->frame_width, vid->frame_height, rgb32 ? 32 : 16, 0, 0, 0, 0);
         vid->screen_fence[i] = 0;
         if (!vid->screens[i]) {
            ok = false;
            break;
         }
      }
      if (!ok) {
         RARCH_ERR("[MI_GFX]: Failed to init GFX surface\n");
         for (unsigned i = 0; i < 2; i++) {
            if (vid->screens[i]) {
               GFX_FreeSurface(vid->screens[i]);
               vid->screens[i] = NULL;
            }
         }
         vid->screen = NULL;
         return;
      }
      vid->screen_index = 1;
      vid->screen       = vid->screens[vid->screen_index];
   }

   if (!vid->menu_active && (viewport_changed || surface_size_changed || surface_format_changed))
      sdl_miyoomini_clear_border(fb_addr, vid->video_x, vid->video_y, vid->video_w, vid->video_h);

   vid->rgb32 = rgb32;
}

static void *sdl_miyoomini_gfx_init(const video_info_t *video,
      input_driver_t **input, void **input_data) {
   sdl_miyoomini_video_t *vid                    = NULL;
   uint32_t sdl_subsystem_flags                  = SDL_WasInit(0);
   settings_t *settings                          = config_get_ptr();
   const char *input_drv_name                 = settings->arrays.input_driver;
   const char *joypad_drv_name                = settings->arrays.input_joypad_driver;

   sdl_miyoomini_set_cpugovernor(PERFORMANCE);

   /* Initialise graphics subsystem, if required */
   if (sdl_subsystem_flags == 0) {
      if (SDL_Init(SDL_INIT_VIDEO) < 0) return NULL;
   } else if ((sdl_subsystem_flags & SDL_INIT_VIDEO) == 0) {
      if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) return NULL;
   }

   vid = (sdl_miyoomini_video_t*)calloc(1, sizeof(*vid));
   if (!vid) return NULL;

#if SDL_MAJOR_VERSION >= 2
   /* Keep SDL2 from applying filtered renderer scaling -
    * Miyoomini uses Dingux MI_GFX scaling/viewport path */
   SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#endif

#ifdef HAVE_CHEEVOS
   vid->cheevos_lock = SDL_CreateMutex();
   if (!vid->cheevos_lock)
   {
      RARCH_ERR("[SDL1]: Failed to create cheevos mutex\n");
      free(vid);
      return NULL;
   }
#endif

   GFX_Init();

   vid->menuscreen = GFX_CreateRGBSurface(
         0, SDL_MIYOOMINI_WIDTH, SDL_MIYOOMINI_HEIGHT, 16, 0, 0, 0, 0);
   vid->menuscreen_rgui = GFX_CreateRGBSurface(
         0, RGUI_MENU_WIDTH, RGUI_MENU_HEIGHT, 16, 0, 0, 0, 0);

   if (!vid->menuscreen||!vid->menuscreen_rgui) {
      RARCH_ERR("[MI_GFX]: Failed to init GFX surface\n");
      goto error;
   }

   vid->content_width     = SDL_MIYOOMINI_WIDTH;
   vid->content_height    = SDL_MIYOOMINI_HEIGHT;
   vid->rgb32             = video->rgb32;
   vid->vsync             = video->vsync;
   vid->keep_aspect       = settings->bools.video_dingux_ipu_keep_aspect;
   vid->scale_integer     = settings->bools.video_scale_integer;
   vid->filter_type       = (enum dingux_ipu_filter_type)settings->uints.video_dingux_ipu_filter_type;
   vid->menu_active       = false;
   vid->was_in_menu       = false;
   vid->menu_texture_alpha = 1.0f;
   vid->menu_bg_valid     = false;
   vid->quitting          = false;
   vid->ff_frame_time_min = 16667;
   vid->ff_refresh_rate_cached = 0.0f;

   sdl_miyoomini_set_output(vid, vid->content_width, vid->content_height, vid->rgb32);

   GFX_SetFlipFlags(vid->vsync ? (GFX_BLOCKING | GFX_FLIPWAIT) : 0);

   sdl_miyoomini_input_driver_init(input_drv_name,
         joypad_drv_name, input, input_data);

   /* Initialise OSD font */
   sdl_miyoomini_init_font_color(vid);
   sdl_miyoomini_update_msg_bg_cache(vid, settings);

   vid->osd_font = bitmapfont_get_lut();

   if (!vid->osd_font ||
       vid->osd_font->glyph_max <
            (SDL_NUM_FONT_GLYPHS - 1)) {
      RARCH_ERR("[SDL1]: Failed to init OSD font\n");
      goto error;
   }

   return vid;

error:
   sdl_miyoomini_gfx_free(vid);
   return NULL;
}

static bool sdl_miyoomini_gfx_frame(void *data, const void *frame,
      unsigned width, unsigned height, uint64_t frame_count,
      unsigned pitch, const char *msg, video_frame_info_t *video_info) {
   sdl_miyoomini_video_t* vid = (sdl_miyoomini_video_t*)data;
#ifdef HAVE_MENU
   bool menu_is_alive      = (video_info->menu_st_flags & MENU_ST_FLAG_ALIVE) ? true : false;
#endif

   /* Return early if:
    * - Input sdl_miyoomini_video_t struct is NULL
    *   (cannot realistically happen)
    * - Menu is inactive and input 'content' frame
    *   data is NULL (may happen when e.g. a running
    *   core skips a frame) */
   if (unlikely(!vid || (!frame && !vid->menu_active))) return true;

   /* If fast forward is currently active, we may
    * push frames at an 'unlimited' rate. Since the
    * display has a fixed refresh rate of 60 Hz, this
    * represents wasted effort. We therefore drop any
    * 'excess' frames in this case.
    * (Note that we *only* do this when fast forwarding.
    * Attempting this trick while running content normally
    * will cause bad frame pacing) */
   if (unlikely(video_info->input_driver_nonblock_state)) {
      retro_time_t current_time = cpu_features_get_time_usec();
      retro_time_t ff_frame_time_min = vid->ff_frame_time_min;

      if (video_info->refresh_rate > 1.0f
            && vid->ff_refresh_rate_cached != video_info->refresh_rate)
      {
         ff_frame_time_min = (retro_time_t)(1000000.0f / video_info->refresh_rate + 0.5f);
         if (ff_frame_time_min == 0)
            ff_frame_time_min = 1;
         vid->ff_frame_time_min = ff_frame_time_min;
         vid->ff_refresh_rate_cached = video_info->refresh_rate;
      }

      if ((current_time - vid->last_frame_time) < ff_frame_time_min)
         return true;

      vid->last_frame_time = current_time;
   }

#ifdef HAVE_MENU
    menu_driver_frame(menu_is_alive, video_info);
#endif

#ifdef HAVE_CHEEVOS
   bool achievement_msg_active = video_info->msg_queue_icon == MESSAGE_QUEUE_ICON_ACHIEVEMENT
         && video_info->msg_queue_title[0];
   sdl_miyoomini_cheevos_lock(vid);
   if (achievement_msg_active
         || vid->cheevos_icon_timer > 0
         || vid->cheevos_icon_restore_pending
         || (vid->cheevos_badge_pending[0] && vid->cheevos_icon_retry_counter > 0))
   {
      /* Keep icon visibility strictly tied to active achievement notifications. */
      if (!achievement_msg_active)
         vid->cheevos_icon_timer = 0;

      if (achievement_msg_active)
      {
      bool badge_changed = !string_is_equal(video_info->msg_queue_title, vid->cheevos_badge_pending);

      if (badge_changed)
      {
         /* Keep currently displayed badge until the next one is
          * successfully loaded to avoid popup icon flicker. */
         strlcpy(vid->cheevos_badge_pending, video_info->msg_queue_title, sizeof(vid->cheevos_badge_pending));
         vid->cheevos_icon_retry_counter = 0;
      }

      if (vid->cheevos_badge_pending[0] != '\0')
      {
         if (vid->cheevos_icon_retry_counter == 0
               && (!vid->cheevos_icon_data
                  || !string_is_equal(vid->cheevos_badge_pending,
                        vid->cheevos_badge_name)))
         {
            sdl_miyoomini_load_cheevos_icon(
                  vid, vid->cheevos_badge_pending, SDL_MIYOOMINI_HEIGHT);

            vid->cheevos_icon_retry_counter = 30;
         }
      }

      if (vid->cheevos_icon_data)
      {
         vid->cheevos_icon_visible = true;
         if (badge_changed || vid->cheevos_icon_timer == 0)
            vid->cheevos_icon_timer = video_info->msg_queue_duration > 0
                  ? video_info->msg_queue_duration
                  : CHEEVOS_ICON_DURATION_FRAMES;
      }
      else if (vid->cheevos_icon_timer == 0)
         vid->cheevos_icon_visible = false;
      }
      else if (vid->cheevos_icon_timer > 0)
      {
      if (vid->cheevos_icon_retry_counter > 0)
         vid->cheevos_icon_retry_counter--;

      if (vid->cheevos_icon_data)
         vid->cheevos_icon_visible = true;
      else if (vid->cheevos_badge_pending[0] != '\0')
      {
         if (vid->cheevos_icon_retry_counter == 0)
         {
            if (sdl_miyoomini_load_cheevos_icon(
                     vid, vid->cheevos_badge_pending, SDL_MIYOOMINI_HEIGHT))
            {
               vid->cheevos_icon_visible = true;
            }
            else
            {
               vid->cheevos_icon_visible = false;
            }

            vid->cheevos_icon_retry_counter = 30;
         }
         else
            vid->cheevos_icon_visible = false;
      }
      else
         vid->cheevos_icon_visible = false;
      }
      else
      {
         if (vid->cheevos_icon_visible)
         {
            vid->cheevos_icon_visible = false;
            vid->cheevos_icon_restore_pending = true;
         }
         else
         {
            vid->cheevos_icon_visible = false;
            vid->cheevos_icon_retry_counter = 0;
            vid->cheevos_badge_pending[0] = '\0';
         }
      }
   }
   sdl_miyoomini_cheevos_unlock(vid);
#endif

   /* Render OSD text at flip */
   if (msg
#ifdef HAVE_CHEEVOS
         || vid->cheevos_icon_timer > 0
         || vid->cheevos_icon_restore_pending
#endif
      ) {
      sdl_miyoomini_update_msg_cache(vid, msg);
      if (!vid->flip_callback_active)
      {
         GFX_SetFlipCallback(sdl_miyoomini_print_msg, vid);
         vid->flip_callback_active = true;
      }
   } else if (vid->msg_count) {
      sdl_miyoomini_update_msg_cache(vid, NULL);
      if (!vid->flip_callback_active)
      {
         GFX_SetFlipCallback(sdl_miyoomini_print_msg, vid);
         vid->flip_callback_active = true;
      }
   } else {
      if (vid->flip_callback_active)
      {
         GFX_SetFlipCallback(NULL, NULL);
         vid->flip_callback_active = false;
      }
   }

   if (likely(!vid->menu_active)) {
      /* Clear border if we were in the menu on the previous frame */
      if (unlikely(vid->was_in_menu)) {
         sdl_miyoomini_clear_border(fb_addr, vid->video_x, vid->video_y, vid->video_w, vid->video_h);
         vid->was_in_menu = false;
      }
      /* Update video mode if width/height have changed */
      if (unlikely( (vid->content_width  != width ) ||
                    (vid->content_height != height) )) {
         sdl_miyoomini_set_output(vid, width, height, vid->rgb32);
      }
      {
         unsigned next_index = vid->screen_index ^ 1;
         unsigned target_slot = vid->screens[next_index] ? next_index : vid->screen_index;
         unsigned fallback_slot = target_slot ^ 1;
         if (fallback_slot < 2
               && vid->screens[fallback_slot]
               && !vid->screen_fence[fallback_slot])
            target_slot = fallback_slot;
         if (vid->screen_fence[target_slot]) {
            MI_GFX_WaitAllDone(FALSE, vid->screen_fence[target_slot]);
            vid->screen_fence[target_slot] = 0;
         }
         vid->screen_index = target_slot;
         vid->screen       = vid->screens[target_slot];
      }
      if (unlikely(!vid->screen))
         return false;
      /* SW Blit frame to GFX surface with scaling */
      vid->scale_func(vid, (void*)frame, vid->screen->pixels, width, height, pitch, vid->screen->pitch);
      /* HW Blit GFX surface to Framebuffer and Flip */
      GFX_UpdateRect(vid->screen, vid->video_x, vid->video_y, vid->video_w, vid->video_h);
      vid->screen_fence[vid->screen_index] = flipFence;
   } else {
      if ((vid->menu_texture_alpha < 0.999f) && vid->menu_bg_valid)
      {
         uint8_t alpha_u8 = (uint8_t)(vid->menu_texture_alpha * 255.0f);

         if (vid->menu_surface_dirty || !vid->menu_composite_valid
               || (vid->menu_texture_alpha_u8 != alpha_u8))
         {
            unsigned i;
            uint16_t *menu_rgui_src = (uint16_t*)vid->menuscreen_rgui->pixels;

            for (i = 0; i < (RGUI_MENU_WIDTH * RGUI_MENU_HEIGHT); i++)
            {
               uint16_t fg      = menu_rgui_src[i];
               vid->menu_composite_texture[i] = (fg != 0)
                     ? fg
                     : sdl_miyoomini_blend_565(
                           vid->menu_bg_texture[i],
                           fg,
                           alpha_u8);
            }

            scale2x2_n16(vid->menu_composite_texture, vid->menuscreen->pixels,
                  RGUI_MENU_WIDTH, RGUI_MENU_HEIGHT, 0, 0);
            vid->menu_texture_alpha_u8 = alpha_u8;
            vid->menu_composite_valid  = true;
            vid->menu_surface_dirty    = false;
         }
      }
      else
      {
         if (vid->menu_surface_dirty || vid->menu_composite_valid)
         {
            scale2x2_n16(vid->menuscreen_rgui->pixels, vid->menuscreen->pixels,
                  RGUI_MENU_WIDTH, RGUI_MENU_HEIGHT, 0, 0);
            vid->menu_surface_dirty   = false;
            vid->menu_composite_valid = false;
         }
      }
      stOpt.eRotate = E_MI_GFX_ROTATE_180;
      GFX_Flip(vid->menuscreen);
      stOpt.eRotate = vid->rotate;
   }
   return true;
}

static void sdl_miyoomini_set_texture_enable(void *data, bool state, bool full_screen) {
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   if (unlikely(!vid)) return;

   if (state == vid->menu_active) return;
   vid->menu_active = state;

   sdl_miyoomini_toggle_powersave(state);

	  if (state) {
	     sdl_miyoomini_capture_menu_background(vid);
	  //    system("playActivity stop_all &");
	     vid->menu_surface_dirty = true;
	     vid->was_in_menu = true;
	  }
	  else {
	     vid->menu_bg_valid = false;
	     vid->menu_surface_dirty = true;
	     vid->menu_composite_valid = false;
	  }
  // else {
  //   system("playActivity resume &");
  // }
}

static void sdl_miyoomini_set_texture_frame(void *data, const void *frame, bool rgb32,
      unsigned width, unsigned height, float alpha) {
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   unsigned x, y;
   unsigned x_step, y_step, y_acc;

   if (unlikely(!vid || !frame || !width || !height))
      return;

   vid->menu_texture_alpha = alpha;
   vid->menu_surface_dirty = true;

   if (unlikely(width != vid->menu_src_width_cached
         || height != vid->menu_src_height_cached
         || rgb32 != vid->menu_src_rgb32_cached))
   {
      vid->menu_src_width_cached  = width;
      vid->menu_src_height_cached = height;
      vid->menu_src_rgb32_cached  = rgb32;
      vid->menu_x_step_cached     = (width << 16) / RGUI_MENU_WIDTH;
      vid->menu_y_step_cached     = (height << 16) / RGUI_MENU_HEIGHT;
   }

   if (!rgb32) {
      const uint16_t *src = (const uint16_t*)frame;
      uint16_t *dst       = (uint16_t*)vid->menuscreen_rgui->pixels;

      if ((width == RGUI_MENU_WIDTH) && (height == RGUI_MENU_HEIGHT))
      {
         memcpy_neon(dst, (void*)src, RGUI_MENU_WIDTH * RGUI_MENU_HEIGHT * sizeof(uint16_t));
         return;
      }

      x_step = vid->menu_x_step_cached;
      y_step = vid->menu_y_step_cached;
      y_acc  = 0;
      for (y = 0; y < RGUI_MENU_HEIGHT; y++) {
         unsigned sy = y_acc >> 16;
         unsigned x_acc = 0;
         for (x = 0; x < RGUI_MENU_WIDTH; x++) {
            unsigned sx = x_acc >> 16;
            dst[(y * RGUI_MENU_WIDTH) + x] = src[(sy * width) + sx];
            x_acc += x_step;
         }
         y_acc += y_step;
      }
      return;
   }

   {
      const uint32_t *src = (const uint32_t*)frame;
      uint16_t *dst       = (uint16_t*)vid->menuscreen_rgui->pixels;
      unsigned total_pixels;

      if ((width == RGUI_MENU_WIDTH) && (height == RGUI_MENU_HEIGHT))
      {
         total_pixels = RGUI_MENU_WIDTH * RGUI_MENU_HEIGHT;
         for (x = 0; x < total_pixels; x++)
         {
            uint32_t p = src[x];
            dst[x] = ((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F);
         }
         return;
      }

      x_step = vid->menu_x_step_cached;
      y_step = vid->menu_y_step_cached;
      y_acc  = 0;
      for (y = 0; y < RGUI_MENU_HEIGHT; y++) {
         unsigned sy = y_acc >> 16;
         unsigned x_acc = 0;
         for (x = 0; x < RGUI_MENU_WIDTH; x++) {
            unsigned sx = x_acc >> 16;
            uint32_t c  = src[(sy * width) + sx];
            uint8_t r   = (c >> 16) & 0xFF;
            uint8_t g   = (c >> 8) & 0xFF;
            uint8_t b   = c & 0xFF;

            dst[(y * RGUI_MENU_WIDTH) + x] = (uint16_t)(((r & 0xF8) << 8) |
                                                        ((g & 0xFC) << 3) |
                                                        (b >> 3));
            x_acc += x_step;
         }
         y_acc += y_step;
      }
   }
}

static void sdl_miyoomini_gfx_set_nonblock_state(void *data, bool toggle,
      bool adaptive_vsync_enabled, unsigned swap_interval) {
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   if (unlikely(!vid)) return;

   bool vsync            = !toggle;

   /* Check whether vsync status has changed */
   if (vid->vsync != vsync)
   {
      vid->vsync              = vsync;
      GFX_SetFlipFlags(vsync ? (GFX_BLOCKING | GFX_FLIPWAIT) : 0);
   }
}

static void sdl_miyoomini_gfx_check_window(sdl_miyoomini_video_t *vid) {
   SDL_Event event;

   SDL_PumpEvents();
   while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) == 1)
   {
      if (event.type != SDL_QUIT)
         continue;

      vid->quitting = true;
      sdl_miyoomini_set_cpugovernor(ONDEMAND);
      break;
   }
}

static bool sdl_miyoomini_gfx_alive(void *data) {
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   if (unlikely(!vid)) return false;

   sdl_miyoomini_gfx_check_window(vid);
   return !vid->quitting;
}

static bool sdl_miyoomini_gfx_focus(void *data) { return true; }
static bool sdl_miyoomini_gfx_suppress_screensaver(void *data, bool enable) { return false; }
static bool sdl_miyoomini_gfx_has_windowed(void *data) { return false; }

static void sdl_miyoomini_gfx_set_rotation(void *data, unsigned rotation) {
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   if (unlikely(!vid)) return;
   switch (rotation) {
      case 1:
         stOpt.eRotate = E_MI_GFX_ROTATE_90; break;
      case 2:
         stOpt.eRotate = E_MI_GFX_ROTATE_0; break;
      case 3:
         stOpt.eRotate = E_MI_GFX_ROTATE_270; break;
      default:
         stOpt.eRotate = E_MI_GFX_ROTATE_180; break;
   }
   if (vid->rotate != stOpt.eRotate) {
      vid->rotate = stOpt.eRotate;
      sdl_miyoomini_set_output(vid, vid->content_width, vid->content_height, vid->rgb32);
   }
}

static void sdl_miyoomini_gfx_viewport_info(void *data, struct video_viewport *vp) {
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   if (unlikely(!vid)) return;

   vp->x = vp->y = 0;
   vp->width  = vp->full_width  = vid->content_width;
   vp->height = vp->full_height = vid->content_height;
}

static float sdl_miyoomini_get_refresh_rate(void *data) { return 60.0f; }

static void sdl_miyoomini_set_filtering(void *data, unsigned index, bool smooth, bool ctx_scaling) {
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   settings_t *settings       = config_get_ptr();
   if (unlikely(!vid || !settings)) return;

   enum dingux_ipu_filter_type ipu_filter_type = (settings) ?
         (enum dingux_ipu_filter_type)settings->uints.video_dingux_ipu_filter_type :
         DINGUX_IPU_FILTER_BICUBIC;

   /* Update software filter setting, if required */
   if (vid->filter_type != ipu_filter_type) {
      vid->filter_type = ipu_filter_type;
      sdl_miyoomini_set_output(vid, vid->content_width, vid->content_height, vid->rgb32);
   }
}

static void sdl_miyoomini_apply_state_changes(void *data) {
   sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t*)data;
   settings_t *settings       = config_get_ptr();
   if (unlikely(!vid || !settings)) return;

   bool keep_aspect       = (settings) ? settings->bools.video_dingux_ipu_keep_aspect : true;
   bool integer_scaling   = (settings) ? settings->bools.video_scale_integer : false;

   if ((vid->keep_aspect != keep_aspect) ||
       (vid->scale_integer != integer_scaling)) {
      vid->keep_aspect   = keep_aspect;
      vid->scale_integer = integer_scaling;

      /* Aspect/scaling changes require all frame
       * dimension/padding/cropping parameters to
       * be recalculated. Easiest method is to just
       * (re-)set the current output video mode */
      sdl_miyoomini_set_output(vid, vid->content_width, vid->content_height, vid->rgb32);
   }

   sdl_miyoomini_update_msg_bg_cache(vid, settings);
}

static void sdl_miyoomini_update_msg_bg_cache(
      sdl_miyoomini_video_t *vid, const settings_t *settings)
{
   float opacity = 0.0f;

   if (unlikely(!vid))
      return;

   vid->msg_bg_enable_cached = settings && settings->bools.video_msg_bgcolor_enable;

   if (!vid->msg_bg_enable_cached || !settings)
   {
      vid->msg_bg_alpha_cached = 0;
      vid->msg_bg_rgb_cached   = 0;
      return;
   }

   opacity = settings->floats.video_msg_bgcolor_opacity;
   if (opacity < 0.0f)
      opacity = 0.0f;
   else if (opacity > 1.0f)
      opacity = 1.0f;

   vid->msg_bg_alpha_cached = (uint8_t)(opacity * 255.0f);
   vid->msg_bg_rgb_cached   = ((settings->uints.video_msg_bgcolor_red   & 0xFF) << 16)
                            | ((settings->uints.video_msg_bgcolor_green & 0xFF) << 8)
                            |  (settings->uints.video_msg_bgcolor_blue  & 0xFF);
}

static void sdl_miyoomini_update_msg_cache(
      sdl_miyoomini_video_t *vid, const char *msg)
{
   uint32_t i;
   uint32_t msg_len = 0;
   const char *src = msg ? msg : "";

   if (unlikely(!vid))
      return;

   if (string_is_equal(vid->msg_tmp, src))
      return;

   strlcpy(vid->msg_tmp, src, sizeof(vid->msg_tmp));
   msg_len = strlen_size(vid->msg_tmp, OSD_TEXT_LEN_MAX);
   vid->msg_len_cached = msg_len;
   vid->msg_line_count_cached = msg_len ? (uint32_t)((msg_len - 1) / OSD_TEXT_LINE_LEN) + 1 : 0;
   if (vid->msg_line_count_cached > OSD_TEXT_LINES_MAX)
      vid->msg_line_count_cached = OSD_TEXT_LINES_MAX;

   memset(vid->msg_line_chars_cached, 0, sizeof(vid->msg_line_chars_cached));
   for (i = 0; i < msg_len; i++)
   {
      uint32_t line = i / OSD_TEXT_LINE_LEN;
      if (line >= OSD_TEXT_LINES_MAX)
         break;
      vid->msg_line_chars_cached[line]++;
   }
}

static uint32_t sdl_miyoomini_get_flags(void *data) { return 0; }

static const video_poke_interface_t sdl_miyoomini_poke_interface = {
   sdl_miyoomini_get_flags,
   NULL, /* load_texture */
   NULL, /* unload_texture */
   NULL, /* set_video_mode */
   sdl_miyoomini_get_refresh_rate,
   sdl_miyoomini_set_filtering,
   NULL, /* get_video_output_size */
   NULL, /* get_video_output_prev */
   NULL, /* get_video_output_next */
   NULL, /* get_current_framebuffer */
   NULL, /* get_proc_address */
   NULL, /* set_aspect_ratio */
   sdl_miyoomini_apply_state_changes,
   sdl_miyoomini_set_texture_frame,
   sdl_miyoomini_set_texture_enable,
   NULL, /* set_osd_msg */
   NULL, /* sdl_show_mouse */
   NULL, /* sdl_grab_mouse_toggle */
   NULL, /* get_current_shader */
   NULL, /* get_current_software_framebuffer */
   NULL, /* get_hw_render_interface */
   NULL, /* set_hdr_max_nits */
   NULL, /* set_hdr_paper_white_nits */
   NULL, /* set_hdr_contrast */
   NULL  /* set_hdr_expand_gamut */
};

static void sdl_miyoomini_get_poke_interface(void *data, const video_poke_interface_t **iface) {
   *iface = &sdl_miyoomini_poke_interface;
}

static bool sdl_miyoomini_gfx_set_shader(void *data,
      enum rarch_shader_type type, const char *path) { return false; }

#ifdef HAVE_OVERLAY

static void sdl_miyoomini_overlay_enable(void *data, bool state) {
	sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t *)data;
	if (!vid) return;

	if ((state)&&(vid->overlay_surface)) GFX_SetupOverlaySurface(vid->overlay_surface);
	else GFX_SetupOverlaySurface(NULL);
}

static bool sdl_miyoomini_overlay_load(void *data, const void *image_data, unsigned num_images) {
	sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t *)data;
	if (!vid) return false;

	struct texture_image *images = (struct texture_image *)image_data;
	void* pixels = images[0].pixels;
	uint32_t width = images[0].width;
	uint32_t height = images[0].height;
	uint32_t rmask = images[0].supports_rgba ? 0x000000FF : 0x00FF0000;
	uint32_t bmask = images[0].supports_rgba ? 0x00FF0000 : 0x000000FF;

	if (vid->overlay_surface) GFX_FreeSurface(vid->overlay_surface);
	SDL_Surface *ostmp = SDL_CreateRGBSurfaceFrom(pixels, width, height, 32, width*4,
				rmask, 0x0000FF00, bmask, 0xFF000000);
	SDL_Surface *ostmp2 = GFX_DuplicateSurface(ostmp);
	SDL_FreeSurface(ostmp);
	vid->overlay_surface = GFX_CreateRGBSurface(0, 640, 480, 32,
				rmask, 0x0000FF00, bmask, 0xFF000000);
	SDL_SetSurfaceBlendMode(ostmp2, SDL_BLENDMODE_NONE);
	GFX_BlitSurfaceRotate(ostmp2, NULL, vid->overlay_surface, NULL, 2);
	GFX_FreeSurface(ostmp2);

	settings_t *settings = config_get_ptr();
	vid->overlay_surface->flags |= SDL_SRCALPHA;
	SDL_SetSurfaceBlendMode(vid->overlay_surface, SDL_BLENDMODE_BLEND);
	SDL_SetSurfaceAlphaMod(vid->overlay_surface, (settings) ? settings->floats.input_overlay_opacity * 0xFF : 255);
	GFX_SetupOverlaySurface(vid->overlay_surface);

	return true;
}

static void sdl_miyoomini_overlay_tex_geom(void *data, unsigned idx, float x, float y, float w, float h) { }
static void sdl_miyoomini_overlay_vertex_geom(void *data, unsigned idx, float x, float y, float w, float h) { }
static void sdl_miyoomini_overlay_full_screen(void *data, bool enable) { }

static void sdl_miyoomini_overlay_set_alpha(void *data, unsigned idx, float mod) {
	sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t *)data;
	if ((!idx)&&(vid)&&(vid->overlay_surface)) {
		uint8_t value = mod * 0xFF;
		vid->overlay_surface->flags |= SDL_SRCALPHA;
		SDL_SetSurfaceBlendMode(vid->overlay_surface, SDL_BLENDMODE_BLEND);
		{
			Uint8 current_alpha = 0;
			SDL_GetSurfaceAlphaMod(vid->overlay_surface, &current_alpha);
			if (current_alpha != value) {
				SDL_SetSurfaceAlphaMod(vid->overlay_surface, value);
				GFX_SetupOverlaySurface(vid->overlay_surface);
			}
		}
	}
	return;
}

static const video_overlay_interface_t sdl_miyoomini_overlay = {
	sdl_miyoomini_overlay_enable,
	sdl_miyoomini_overlay_load,
	sdl_miyoomini_overlay_tex_geom,
	sdl_miyoomini_overlay_vertex_geom,
	sdl_miyoomini_overlay_full_screen,
	sdl_miyoomini_overlay_set_alpha,
};

void sdl_miyoomini_gfx_get_overlay_interface(void *data, const video_overlay_interface_t **iface)
{
    sdl_miyoomini_video_t *vid = (sdl_miyoomini_video_t *)data;
    if (!vid) return;
    *iface = &sdl_miyoomini_overlay;
}

#endif

video_driver_t video_sdl2 = {
   sdl_miyoomini_gfx_init,
   sdl_miyoomini_gfx_frame,
   sdl_miyoomini_gfx_set_nonblock_state,
   sdl_miyoomini_gfx_alive,
   sdl_miyoomini_gfx_focus,
   sdl_miyoomini_gfx_suppress_screensaver,
   sdl_miyoomini_gfx_has_windowed,
   sdl_miyoomini_gfx_set_shader,
   sdl_miyoomini_gfx_free,
   "sdl2",
   NULL, /* set_viewport */
   sdl_miyoomini_gfx_set_rotation,
   sdl_miyoomini_gfx_viewport_info,
   NULL, /* read_viewport  */
   NULL, /* read_frame_raw */
#ifdef HAVE_OVERLAY
   sdl_miyoomini_gfx_get_overlay_interface,
#endif
   sdl_miyoomini_get_poke_interface
};
