#ifndef AUDIO_DRIVERS_MIYOOMINI_AUDIO_COMMON_H
#define AUDIO_DRIVERS_MIYOOMINI_AUDIO_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#define MIYOO_AUDIO_CHANNELS            2u
#define MIYOO_AUDIO_BYTES_PER_SAMPLE    2u
#define MIYOO_AUDIO_TARGET_LATENCY_MS   18u
#define MIYOO_AUDIO_SLEEP_MIN_US        400u
#define MIYOO_AUDIO_SLEEP_MAX_US        4000u
#define MIYOO_AUDIOSERVER_FIFO          "/tmp/audio_fifo_server"

typedef struct miyoo_audio_timing
{
   uint32_t rate;
   uint32_t bytes_per_sec;
   uint32_t target_bytes;
   uint32_t high_water;
   uint32_t low_water;
} miyoo_audio_timing_t;

static inline uint32_t miyoo_audio_select_rate(uint32_t requested)
{
   static const uint32_t table[] =
   { 8000u, 11025u, 12000u, 16000u, 22050u, 24000u, 32000u, 44100u, 48000u };
   size_t i;

   if (!requested)
      return table[0];

   for (i = 0; i < sizeof(table) / sizeof(table[0]); i++)
   {
      if (requested <= table[i])
         return table[i];
   }

   return table[(sizeof(table) / sizeof(table[0])) - 1];
}

static inline uint32_t miyoo_audio_bytes_per_sec(uint32_t rate)
{
   return rate * MIYOO_AUDIO_CHANNELS * MIYOO_AUDIO_BYTES_PER_SAMPLE;
}

static inline void miyoo_audio_timing_init(miyoo_audio_timing_t *timing,
      uint32_t rate, size_t hw_buf_bytes)
{
   uint32_t bytes_per_sec;
   uint32_t target;
   uint32_t hw_quarter;

   if (!timing)
      return;

   if (!hw_buf_bytes)
      hw_buf_bytes = 1;

   timing->rate         = rate;
   timing->bytes_per_sec = miyoo_audio_bytes_per_sec(rate);
   bytes_per_sec         = timing->bytes_per_sec ? timing->bytes_per_sec : 1;

   target = (bytes_per_sec * MIYOO_AUDIO_TARGET_LATENCY_MS) / 1000u;
   hw_quarter = (uint32_t)(hw_buf_bytes / 4u);

   if (target < hw_quarter)
      target = hw_quarter;
   if (target > hw_buf_bytes)
      target = (uint32_t)hw_buf_bytes;

   timing->target_bytes = target;
   timing->high_water   = target + hw_quarter;
   if (timing->high_water > hw_buf_bytes)
      timing->high_water = (uint32_t)hw_buf_bytes;

   timing->low_water = target / 2u;
   if (!timing->low_water)
      timing->low_water = target / 4u;
   if (!timing->low_water)
      timing->low_water = 1u;
}

static inline size_t miyoo_audio_fifo_chunk(const miyoo_audio_timing_t *timing)
{
   size_t chunk;

   if (!timing || !timing->bytes_per_sec)
      return 1024u;

   chunk = timing->target_bytes / 2u;
   if (chunk < 1024u)
      chunk = 1024u;
   if (chunk > 4096u)
      chunk = 4096u;

   return (chunk + 15u) & ~15u;
}

static inline useconds_t miyoo_audio_backpressure(
      const miyoo_audio_timing_t *timing, uint32_t busy_bytes)
{
   uint32_t target;
   uint32_t bytes_per_sec;
   uint32_t low;
   uint32_t high;
   uint64_t wait_us;

   if (!timing)
      return 0;

   bytes_per_sec = timing->bytes_per_sec;
   if (!bytes_per_sec)
      return 0;

   target = timing->target_bytes;
   high   = timing->high_water;
   low    = timing->low_water;

   if (busy_bytes > high)
   {
      uint32_t excess = busy_bytes - target;
      wait_us = (uint64_t)excess * 1000000ULL / bytes_per_sec;

      if (wait_us < MIYOO_AUDIO_SLEEP_MIN_US)
         wait_us = MIYOO_AUDIO_SLEEP_MIN_US;
      else if (wait_us > MIYOO_AUDIO_SLEEP_MAX_US)
         wait_us = MIYOO_AUDIO_SLEEP_MAX_US;

      return (useconds_t)wait_us;
   }

   if (busy_bytes < low)
      return MIYOO_AUDIO_SLEEP_MIN_US;

   return 0;
}

static inline bool miyoo_audio_server_available(void)
{
   struct stat st;

   if (stat(MIYOO_AUDIOSERVER_FIFO, &st) != 0)
      return false;

   return S_ISFIFO(st.st_mode);
}

#endif
