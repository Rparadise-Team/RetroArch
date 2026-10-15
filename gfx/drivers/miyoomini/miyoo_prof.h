/* Miyoo Mini per-frame profiler (diagnostic builds only).
 * Build with: make -f Makefile.<variant> MIYOO_PROFILE=1
 * Every 300 video frames a "[MPROF]" line is written to stderr with the
 * average CPU (thread) and wall time per frame of each section. */
#ifndef MIYOO_PROF_H__
#define MIYOO_PROF_H__

#ifdef MIYOO_PROFILE
#include <stdint.h>

enum
{
   MP_CORE = 0,     /* core_run(): whole emulated frame             */
   MP_VFRAME,       /* video_driver_frame()                         */
   MP_FILTER,       /* softfilter                                   */
   MP_SCALE,        /* driver NEON prescale / copy                  */
   MP_FLIP_WAIT,    /* GFX_FlipExec: wait previous blit             */
   MP_FLIP_FLUSH,   /* GFX_FlipExec: cache flush                    */
   MP_FLIP_DMA,     /* GFX_FlipExec: MI_SYS_MemcpyPa to shadow      */
   MP_FLIP_BLIT,    /* GFX_FlipExec: blit + flip request            */
   MP_AUDIO,        /* audio driver write                           */
   MP_INPUT,        /* joypad poll                                  */
   MP_N
};

typedef struct { uint64_t c, w; } mp_t;

void mp_begin(mp_t *t);
void mp_end(int id, mp_t *t);
void mp_frame_done(void);

#define MP_BEGIN(v)   mp_t v; mp_begin(&v)
#define MP_END(id, v) mp_end((id), &v)
#define MP_RESTART(v) mp_begin(&v)
#define MP_FRAME()    mp_frame_done()
#else
#define MP_BEGIN(v)
#define MP_END(id, v)
#define MP_RESTART(v)
#define MP_FRAME()
#endif

#endif
