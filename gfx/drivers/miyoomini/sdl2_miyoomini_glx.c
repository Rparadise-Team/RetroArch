#include "sdl2_miyoomini_glx.h"

#include <stdbool.h>

#include "../../../../miyoo.h"

extern int miyoo_gfx_apply_cpuclock(int clock);

static bool g_cpuclock_ready = false;

static void sdl2_miyoomini_glx_apply_clock_once(void)
{
   if (g_cpuclock_ready)
      return;

   miyoo_gfx_apply_cpuclock(0);
   g_cpuclock_ready = true;
}

void sdl2_miyoomini_glx_init(sdl2_video_t *vid)
{
   (void)vid;
   sdl2_miyoomini_glx_apply_clock_once();
}

void sdl2_miyoomini_glx_frame_begin(sdl2_video_t *vid, unsigned width, unsigned height)
{
   (void)vid;
   (void)width;
   (void)height;
}

void sdl2_miyoomini_glx_frame_end(sdl2_video_t *vid)
{
   (void)vid;
}

void sdl2_miyoomini_glx_free(sdl2_video_t *vid)
{
   (void)vid;
   g_cpuclock_ready = false;
}
