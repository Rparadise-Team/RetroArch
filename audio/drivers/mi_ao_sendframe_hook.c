/*
 * Miyoo Mini MI_AO pacing shim
 * ---------------------------------
 *
 * This file implements an LD_PRELOAD-compatible replacement for
 * MI_AO_SendFrame() that keeps the hardware queue close to a safe
 * latency window.  It supersedes both of the historical
 * "as_preload" binaries shipped with the audioserver because it
 * auto-detects the underlying libmi_ao location. RetroArch can be built
 * with this file to produce a helper shared object:
 *
 *   arm-linux-gnueabihf-gcc -shared -fPIC -O2 -o as_preload.so \
 *      audio/drivers/mi_ao_sendframe_hook.c -ldl
 *
 * Drop the resulting library next to RetroArch and start it with
 *   LD_PRELOAD=./as_preload.so ./retroarch
 *
 * To force a specific libmi_ao, set MI_AO_PRELOAD_SO=/path/libmi_ao.so
 * before launching RetroArch.
 * The shim is completely transparent for builds that do not preload it.
 */

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <mi_ao.h>

#define SAMPLE_RATE_HZ        48000u
#define CHANNELS              2u
#define BYTES_PER_SAMPLE      2u /* S16LE */
#define STREAM_BYTES_PER_SEC  (SAMPLE_RATE_HZ * CHANNELS * BYTES_PER_SAMPLE)

#define TARGET_QUEUE_MS       18u
#define BUSY_TARGET_BYTES     ((STREAM_BYTES_PER_SEC * TARGET_QUEUE_MS) / 1000u)
#define BUSY_HIGH_BYTES       (BUSY_TARGET_BYTES + BUSY_TARGET_BYTES / 2u)
#define BUSY_LOW_BYTES        (BUSY_TARGET_BYTES / 3u)

#define SLEEP_MIN_US          500u
#define SLEEP_MAX_US          3500u
#define SMOOTH_SHIFT          3u

static void *mi_ao_handle;
static MI_S32 (*real_send_frame)(MI_AUDIO_DEV, MI_AO_CHN, MI_AUDIO_Frame_t*, MI_S32);
static MI_S32 (*real_query_state)(MI_AUDIO_DEV, MI_AO_CHN, MI_AO_ChnState_t*);
static pthread_once_t resolver_once = PTHREAD_ONCE_INIT;

#define MI_AO_OVERRIDE_ENV   "MI_AO_PRELOAD_SO"

static const char *const mi_ao_default_paths[] = {
   "/config/lib/libmi_ao.so",   /* MiniUI / Onion */
   "/customer/lib/libmi_ao.so", /* Stock OS revisions */
   "/lib/libmi_ao.so",
   "libmi_ao.so",
   NULL
};

static void *open_mi_ao_handle(void)
{
   const char *override = getenv(MI_AO_OVERRIDE_ENV);
   const char *const *path;
   void *handle = NULL;

   if (override && override[0])
      handle = dlopen(override, RTLD_LAZY);

   if (handle)
      return handle;

   for (path = mi_ao_default_paths; *path; ++path)
   {
#ifdef RTLD_NOLOAD
      handle = dlopen(*path, RTLD_LAZY | RTLD_NOLOAD);
      if (handle)
         return handle;
#endif
      handle = dlopen(*path, RTLD_LAZY);
      if (handle)
         return handle;
   }

   return NULL;
}

static void resolve_mi_ao(void)
{
   mi_ao_handle = open_mi_ao_handle();
   if (!mi_ao_handle)
      return;

   real_send_frame  = (MI_S32 (*)(MI_AUDIO_DEV, MI_AO_CHN, MI_AUDIO_Frame_t*, MI_S32))
      dlsym(mi_ao_handle, "MI_AO_SendFrame");
   real_query_state = (MI_S32 (*)(MI_AUDIO_DEV, MI_AO_CHN, MI_AO_ChnState_t*))
      dlsym(mi_ao_handle, "MI_AO_QueryChnStat");
}

static inline uint32_t busy_to_us(uint32_t bytes)
{
   return (uint32_t)((bytes * 1000000ULL) / STREAM_BYTES_PER_SEC);
}

static void apply_backpressure(uint32_t busy_bytes)
{
   static uint32_t avg_busy = BUSY_TARGET_BYTES;

   avg_busy -= avg_busy >> SMOOTH_SHIFT;
   avg_busy += busy_bytes >> SMOOTH_SHIFT;

   if (busy_bytes > BUSY_HIGH_BYTES || avg_busy > BUSY_TARGET_BYTES)
   {
      uint32_t reference = busy_bytes > BUSY_TARGET_BYTES ?
         busy_bytes : avg_busy;
      uint32_t excess = reference - BUSY_TARGET_BYTES;
      uint32_t wait_us = busy_to_us(excess);

      if (wait_us < SLEEP_MIN_US)
         wait_us = SLEEP_MIN_US;
      else if (wait_us > SLEEP_MAX_US)
         wait_us = SLEEP_MAX_US;

      usleep(wait_us);
   }
   else if (busy_bytes < BUSY_LOW_BYTES && avg_busy < BUSY_LOW_BYTES)
   {
      usleep(SLEEP_MIN_US);
   }
}

MI_S32 MI_AO_SendFrame(MI_AUDIO_DEV dev, MI_AO_CHN ch,
      MI_AUDIO_Frame_t *frame, MI_S32 timeout_ms)
{
   pthread_once(&resolver_once, resolve_mi_ao);

   if (!real_send_frame)
      return -1;

   MI_S32 ret = real_send_frame(dev, ch, frame, 0);

   if (ret != MI_SUCCESS || !real_query_state)
      return ret;

   MI_AO_ChnState_t status;
   if (real_query_state(dev, ch, &status) != MI_SUCCESS)
      return ret;

   apply_backpressure(status.u32ChnBusyNum);
   return ret;
}
