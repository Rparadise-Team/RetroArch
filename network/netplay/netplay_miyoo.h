#ifndef NETPLAY_MIYOO_H__
#define NETPLAY_MIYOO_H__

#include <stdbool.h>

void netplay_miyoo_wifi_powersave(bool netplay_active);
void netplay_miyoo_socket_lowlatency(int fd);
bool netplay_miyoo_has_internet(void);

#endif
