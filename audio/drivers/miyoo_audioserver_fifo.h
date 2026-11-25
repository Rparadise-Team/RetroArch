#ifndef MIYOO_AUDIOSERVER_FIFO_H
#define MIYOO_AUDIOSERVER_FIFO_H

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#define MIYOO_AUDIOSERVER_FIFO "/tmp/audio_fifo_server"
#define MIYOO_FIFO_FRAME_BYTES 4
#define MIYOO_FIFO_WAIT_US 10000 /* HZ=100 */
#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif
#ifndef F_GETPIPE_SZ
#define F_GETPIPE_SZ 1032
#endif

/* Like audioio, reserve half of the requested budget for the server and AO.
 * The kernel rounds its limit to pages, so also enforce the byte limit. */
static size_t miyoo_fifo_configure(int fd, unsigned rate, unsigned latency)
{
   const size_t tick = (rate / 100) * MIYOO_FIFO_FRAME_BYTES;
   const uint64_t request = (uint64_t)rate * latency / 1000 * MIYOO_FIFO_FRAME_BYTES;
   size_t budget = request > 51200 ? 51200 : ((size_t)request + 15) & ~(size_t)15;
   if (budget < tick)
      budget = tick;

   const int granted = fcntl(fd, F_SETPIPE_SZ, (int)budget);
   int capacity = fcntl(fd, F_GETPIPE_SZ);
   if (capacity < 0)
      capacity = granted;

   size_t limit = budget / 2;
   if (limit < tick)
      limit = tick;
   if (capacity > 0 && limit > (size_t)capacity)
      limit = (size_t)capacity;
   RARCH_LOG("[Miyoo audio]: audioserver FIFO capacity=%d limit=%zu\n", capacity, limit);
   if (granted < 0)
      RARCH_WARN("[Miyoo audio]: Could not resize audioserver FIFO; using occupancy limit.\n");
   return limit;
}

static long miyoo_fifo_room(int fd, size_t limit)
{
   int queued;
   /* libpadsp routes ioctl on its /dev/dsp descriptor to OSS emulation.
    * This is a kernel pipe query, not an OSS request. */
   if (syscall(SYS_ioctl, fd, FIONREAD, &queued) < 0 || queued < 0)
      return -1;
   return (size_t)queued < limit ? (long)(limit - (size_t)queued) : 0;
}

static size_t miyoo_fifo_write_avail(int fd, size_t limit)
{
   const long room = miyoo_fifo_room(fd, limit);
   return room < 0 ? limit / 2 : (size_t)room;
}

static bool miyoo_fifo_set_nonblock(int fd, bool enabled)
{
   const int flags = fcntl(fd, F_GETFL);
   return flags >= 0 && fcntl(fd, F_SETFL,
         enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK) == 0;
}

static ssize_t miyoo_fifo_write(int fd, size_t limit, bool nonblock,
      const void *buffer, size_t bytes)
{
   size_t sent = 0;
   bytes &= ~(size_t)(MIYOO_FIFO_FRAME_BYTES - 1);
   while (sent < bytes) {
      size_t count = bytes - sent;
      const long room = miyoo_fifo_room(fd, limit);
      if (count > limit)
         count = limit;
      if (room >= 0 && count > (size_t)room)
         count = (size_t)room;
      else if (room < 0 && nonblock)
         break;

      count &= ~(size_t)(MIYOO_FIFO_FRAME_BYTES - 1);
      if (count) {
         const ssize_t done = write(fd, (const uint8_t*)buffer + sent, count);
         if (done > 0) {
            sent += done;
            continue;
         }
         if (done < 0 && errno == EINTR)
            continue;
         if (done >= 0 || errno != EAGAIN)
            break;
      }
      if (nonblock)
         break;
      usleep(MIYOO_FIFO_WAIT_US);
   }
   return sent;
}

#endif
