/*
 * Unified Miyoo Mini MI_AO pacing shim
 * ------------------------------------
 * Keep the hardware FIFO near ~18 ms of stereo S16 samples so RetroArch and
 * audioserver share the same cadence.  This replaces the "viejo/nuevo"
 * preload binaries – build it once and LD_PRELOAD it for both processes.
 *
 * Build with the union-miyoomini-toolchain:
 *   arm-linux-gnueabihf-gcc -shared -fPIC -O2 \
 *      -o as_preload.so audio/drivers/mi_ao_sendframe_hook.c -ldl -lpthread
 */

#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <mi_ao.h>

#define SAMPLE_RATE_HZ        48000u
#define CHANNELS              2u
#define BYTES_PER_SAMPLE      2u
#define STREAM_BYTES_PER_SEC  (SAMPLE_RATE_HZ * CHANNELS * BYTES_PER_SAMPLE)

#define TARGET_MS             18u
#define TARGET_BYTES          ((STREAM_BYTES_PER_SEC * TARGET_MS) / 1000u)
#define HIGH_WATER_BYTES      (TARGET_BYTES + (TARGET_BYTES / 2u))
#define LOW_WATER_BYTES       (TARGET_BYTES / 2u)

#define SLEEP_MIN_US          400u
#define SLEEP_MAX_US          4000u

#define MI_AO_OVERRIDE_ENV    "MI_AO_PRELOAD_SO"

static void *mi_ao_handle;
static MI_S32 (*real_send_frame)(MI_AUDIO_DEV, MI_AO_CHN, MI_AUDIO_Frame_t*, MI_S32);
static MI_S32 (*real_query_state)(MI_AUDIO_DEV, MI_AO_CHN, MI_AO_ChnState_t*);

static pthread_mutex_t busy_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t filtered_busy;

static const char *const mi_ao_paths[] = {
   "/config/lib/libmi_ao.so",
   "/customer/lib/libmi_ao.so",
   "/lib/libmi_ao.so",
   "libmi_ao.so",
   NULL
};

static void ensure_resolved(void)
{
   const char *override_path = getenv(MI_AO_OVERRIDE_ENV);
   const char *const *cursor = mi_ao_paths;

   if (real_send_frame && real_query_state)
      return;

   if (override_path && override_path[0])
      mi_ao_handle = dlopen(override_path, RTLD_LAZY);

   while (!mi_ao_handle && *cursor)
   {
      mi_ao_handle = dlopen(*cursor, RTLD_LAZY);
      cursor++;
   }

   if (!mi_ao_handle)
      return;

   real_send_frame = (MI_S32 (*)(MI_AUDIO_DEV, MI_AO_CHN, MI_AUDIO_Frame_t*, MI_S32))
      dlsym(mi_ao_handle, "MI_AO_SendFrame");
   real_query_state = (MI_S32 (*)(MI_AUDIO_DEV, MI_AO_CHN, MI_AO_ChnState_t*))
      dlsym(mi_ao_handle, "MI_AO_QueryChnStat");
}

static inline useconds_t busy_wait_time(uint32_t busy_bytes)
{
   uint64_t wait;

   if (busy_bytes > HIGH_WATER_BYTES)
   {
     wait = (uint64_t)(busy_bytes - TARGET_BYTES) * 1000000ULL / STREAM_BYTES_PER_SEC;
     if (wait < SLEEP_MIN_US)
        wait = SLEEP_MIN_US;
     else if (wait > SLEEP_MAX_US)
        wait = SLEEP_MAX_US;
     return (useconds_t)wait;
   }

   if (busy_bytes < LOW_WATER_BYTES)
      return SLEEP_MIN_US;

   return 0;
}

MI_S32 MI_AO_SendFrame(MI_AUDIO_DEV dev, MI_AO_CHN ch,
      MI_AUDIO_Frame_t *frame, MI_S32 timeout_ms)
{
   MI_AO_ChnState_t state;
   MI_S32 ret;
   useconds_t sleep_us = 0;
   uint32_t busy;

   (void)timeout_ms;

   ensure_resolved();
   if (!real_send_frame)
      return -1;

   ret = real_send_frame(dev, ch, frame, 0);
   if (ret != MI_SUCCESS || !real_query_state)
      return ret;

   if (real_query_state(dev, ch, &state) != MI_SUCCESS)
      return ret;

   pthread_mutex_lock(&busy_lock);
   busy = state.u32ChnBusyNum;

   if (!filtered_busy)
      filtered_busy = busy;
   else
      filtered_busy = (filtered_busy * 3u + busy) >> 2;

   sleep_us = busy_wait_time(filtered_busy);
   pthread_mutex_unlock(&busy_lock);

   if (sleep_us)
      usleep(sleep_us);

   return ret;
}
