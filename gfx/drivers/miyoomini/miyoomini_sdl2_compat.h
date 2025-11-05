#ifndef MIYOOMINI_SDL2_COMPAT_H
#define MIYOOMINI_SDL2_COMPAT_H

#include <stdbool.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <mi_sys.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compatibility defines for legacy SDL 1.2 flags used by the Miyoo code */
#ifndef SDL_HWSURFACE
#define SDL_HWSURFACE 0x00000001
#endif

#ifndef SDL_DOUBLEBUF
#define SDL_DOUBLEBUF 0x40000000
#endif

#ifndef SDL_SRCCOLORKEY
#define SDL_SRCCOLORKEY 0x00001000
#endif

#ifndef SDL_SRCALPHA
#define SDL_SRCALPHA 0x00010000
#endif

/* Simple wrapper used to track the physical framebuffer address associated
 * with an SDL surface. SDL 2.0 removed the unused fields that the legacy
 * code repurposed, so we stash the information in the surface userdata. */
typedef struct miyoomini_surface_extra
{
   MI_PHY phys_addr;
} miyoomini_surface_extra_t;

static inline miyoomini_surface_extra_t *miyoomini_surface_extra(SDL_Surface *surface,
      bool create)
{
   miyoomini_surface_extra_t *extra;

   if (!surface)
      return NULL;

   extra = (miyoomini_surface_extra_t*)surface->userdata;
   if (!extra && create)
   {
      extra = (miyoomini_surface_extra_t*)calloc(1, sizeof(*extra));
      if (!extra)
         return NULL;
      surface->userdata = extra;
   }

   return extra;
}

static inline MI_PHY miyoomini_surface_get_phys(SDL_Surface *surface)
{
   miyoomini_surface_extra_t *extra = miyoomini_surface_extra(surface, false);
   return extra ? extra->phys_addr : 0;
}

static inline void miyoomini_surface_set_phys(SDL_Surface *surface, MI_PHY phys)
{
   miyoomini_surface_extra_t *extra = miyoomini_surface_extra(surface, true);
   if (extra)
      extra->phys_addr = phys;
}

static inline void miyoomini_surface_clear_extra(SDL_Surface *surface)
{
   miyoomini_surface_extra_t *extra = miyoomini_surface_extra(surface, false);
   if (extra)
   {
      free(extra);
      surface->userdata = NULL;
   }
}

static inline bool miyoomini_surface_has_colorkey(SDL_Surface *surface,
      Uint32 *color_key)
{
   Uint32 key = 0;

   if (!surface)
      return false;

   if (SDL_GetColorKey(surface, &key) == 0)
   {
      if (color_key)
         *color_key = key;
      return true;
   }

   return false;
}

static inline bool miyoomini_surface_get_alpha(SDL_Surface *surface,
      Uint8 *alpha_mod)
{
   Uint8 alpha = SDL_ALPHA_OPAQUE;

   if (!surface)
      return false;

   if (SDL_GetSurfaceAlphaMod(surface, &alpha) == 0)
   {
      if (alpha_mod)
         *alpha_mod = alpha;
      if (alpha != SDL_ALPHA_OPAQUE)
         return true;
   }

   if (alpha_mod)
      *alpha_mod = alpha;

   /* Treat per-pixel alpha or any active blend mode as SRCALPHA. */
   if (surface->format && surface->format->Amask != 0)
      return true;

   {
      SDL_BlendMode blend_mode = SDL_BLENDMODE_NONE;
      if (SDL_GetSurfaceBlendMode(surface, &blend_mode) == 0 &&
            blend_mode != SDL_BLENDMODE_NONE)
         return true;
   }

   return false;
}

#ifdef __cplusplus
}
#endif

/* External GLES helper hooks provided by the Miyoo-specific SDL driver. */
#ifdef __cplusplus
extern "C" {
#endif
int glUpdateBufferSettings(void *cb);
void glSetMiniRotation(int rotate);
#ifdef __cplusplus
}
#endif

#endif /* MIYOOMINI_SDL2_COMPAT_H */
