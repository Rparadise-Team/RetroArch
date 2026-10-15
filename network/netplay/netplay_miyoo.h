#ifndef NETPLAY_MIYOO_H__
#define NETPLAY_MIYOO_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void netplay_miyoo_wifi_powersave(bool netplay_active);
void netplay_miyoo_socket_lowlatency(int fd);
bool netplay_miyoo_has_internet(void);
uint32_t netplay_miyoo_max_stall_frames(size_t state_size, bool is_server,
      uint32_t max_frames);

#endif
