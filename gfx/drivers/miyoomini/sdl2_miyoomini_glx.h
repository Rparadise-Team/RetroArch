#ifndef SDL2_MIYOOMINI_GLX_H
#define SDL2_MIYOOMINI_GLX_H

#include "../common/sdl2_common.h"

void sdl2_miyoomini_glx_init(sdl2_video_t *vid);
void sdl2_miyoomini_glx_frame_begin(sdl2_video_t *vid, unsigned width, unsigned height);
void sdl2_miyoomini_glx_frame_end(sdl2_video_t *vid);
void sdl2_miyoomini_glx_free(sdl2_video_t *vid);

void sdl2_mmiyoo_glx_init(sdl2_video_t *vid);
void sdl2_mmiyoo_glx_frame_begin(sdl2_video_t *vid, unsigned width, unsigned height);
void sdl2_mmiyoo_glx_frame_end(sdl2_video_t *vid);
void sdl2_mmiyoo_glx_free(sdl2_video_t *vid);

#endif
