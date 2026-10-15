/*  RetroArch - A frontend for libretro.
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

/* Miyoo Mini netplay tuning, kept apart from netplay_frontend.c because
 * the kernel netlink headers clash with the libc network headers. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/nl80211.h>

#include "../../verbosity.h"
#include "netplay_miyoo.h"

#ifndef MIYOO_WIFI_IFACE
#define MIYOO_WIFI_IFACE "wlan0"
#endif

/* true = power saving was on and we turned it off, so it must be restored */
static bool miyoo_wifi_ps_restore = false;

struct miyoo_nl_req
{
   struct nlmsghdr n;
   struct genlmsghdr g;
   char attrs[64];
};

static void miyoo_nl_init(struct miyoo_nl_req *req, uint16_t family, uint8_t cmd)
{
   memset(req, 0, sizeof(*req));
   req->n.nlmsg_len   = NLMSG_LENGTH(GENL_HDRLEN);
   req->n.nlmsg_type  = family;
   req->n.nlmsg_flags = NLM_F_REQUEST;
   req->g.cmd         = cmd;
   req->g.version     = 1;
}

static void miyoo_nl_put(struct miyoo_nl_req *req, uint16_t type,
      const void *data, uint16_t len)
{
   struct nlattr *a = (struct nlattr*)((char*)req + NLMSG_ALIGN(req->n.nlmsg_len));
   a->nla_type      = type;
   a->nla_len       = NLA_HDRLEN + len;
   memcpy((char*)a + NLA_HDRLEN, data, len);
   req->n.nlmsg_len = NLMSG_ALIGN(req->n.nlmsg_len) + NLA_ALIGN(a->nla_len);
}

/* Sends a request and waits for its answer. Returns 0 and the reply in
 * *reply (NULL for a plain ACK), or a negative errno. */
static int miyoo_nl_transact(int fd, struct miyoo_nl_req *req,
      char *buf, size_t size, struct nlmsghdr **reply)
{
   static uint32_t seq = 0;
   struct sockaddr_nl kernel;
   ssize_t len;
   struct nlmsghdr *h;

   memset(&kernel, 0, sizeof(kernel));
   kernel.nl_family   = AF_NETLINK;
   req->n.nlmsg_seq   = ++seq;
   *reply             = NULL;

   if (sendto(fd, req, req->n.nlmsg_len, 0,
            (struct sockaddr*)&kernel, sizeof(kernel)) < 0)
      return -errno;

   for (;;)
   {
      if ((len = recv(fd, buf, size, 0)) < 0)
         return -errno;
      for (h = (struct nlmsghdr*)buf; NLMSG_OK(h, (size_t)len);
            h = NLMSG_NEXT(h, len))
      {
         if (h->nlmsg_seq != req->n.nlmsg_seq)
            continue;
         if (h->nlmsg_type == NLMSG_ERROR)
            return ((struct nlmsgerr*)NLMSG_DATA(h))->error;
         *reply = h;
         return 0;
      }
   }
}

static struct nlattr *miyoo_nl_attr(struct nlmsghdr *h, uint16_t type)
{
   struct nlattr *a = (struct nlattr*)((char*)NLMSG_DATA(h) + GENL_HDRLEN);
   int rem          = (int)h->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);

   while (rem >= NLA_HDRLEN && a->nla_len >= NLA_HDRLEN && a->nla_len <= rem)
   {
      if ((a->nla_type & NLA_TYPE_MASK) == type)
         return a;
      rem -= NLA_ALIGN(a->nla_len);
      a    = (struct nlattr*)((char*)a + NLA_ALIGN(a->nla_len));
   }
   return NULL;
}

#define MIYOO_NLA_U32(a) (*(uint32_t*)((char*)(a) + NLA_HDRLEN))
#define MIYOO_NLA_U16(a) (*(uint16_t*)((char*)(a) + NLA_HDRLEN))

/* While the radio sleeps between beacons, every netplay packet can wait
 * 100 ms or more, which shows up as stalls and input lag. Keep it awake
 * for the whole netplay session. The Realtek 8188fu driver only honours
 * this through nl80211 (its wireless extensions always report power
 * saving as disabled), so talk nl80211 directly. */
void netplay_miyoo_wifi_powersave(bool netplay_active)
{
   static char buf[16384];
   struct miyoo_nl_req req;
   struct sockaddr_nl local;
   struct timeval tv;
   struct nlmsghdr *reply;
   struct nlattr *a;
   uint16_t family;
   uint32_t ifindex, state;
   int fd, ret;

   if (!netplay_active && !miyoo_wifi_ps_restore)
      return;
   if (!(ifindex = if_nametoindex(MIYOO_WIFI_IFACE)))
      return;
   if ((fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC)) < 0)
      return;

   memset(&local, 0, sizeof(local));
   local.nl_family = AF_NETLINK;
   tv.tv_sec       = 0;
   tv.tv_usec      = 500000;
   setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
   if (bind(fd, (struct sockaddr*)&local, sizeof(local)) < 0)
      goto end;

   /* Resolve the nl80211 family id */
   miyoo_nl_init(&req, GENL_ID_CTRL, CTRL_CMD_GETFAMILY);
   miyoo_nl_put(&req, CTRL_ATTR_FAMILY_NAME, NL80211_GENL_NAME,
         sizeof(NL80211_GENL_NAME));
   if (     (ret = miyoo_nl_transact(fd, &req, buf, sizeof(buf), &reply)) < 0
         || !reply
         || !(a = miyoo_nl_attr(reply, CTRL_ATTR_FAMILY_ID)))
   {
      RARCH_WARN("[Netplay] nl80211 not available (%d), Wi-Fi power saving untouched.\n", ret);
      goto end;
   }
   family = MIYOO_NLA_U16(a);

   if (netplay_active)
   {
      if (miyoo_wifi_ps_restore)
         goto end;

      miyoo_nl_init(&req, family, NL80211_CMD_GET_POWER_SAVE);
      miyoo_nl_put(&req, NL80211_ATTR_IFINDEX, &ifindex, sizeof(ifindex));
      if (     (ret = miyoo_nl_transact(fd, &req, buf, sizeof(buf), &reply)) < 0
            || !reply
            || !(a = miyoo_nl_attr(reply, NL80211_ATTR_PS_STATE)))
      {
         RARCH_WARN("[Netplay] Could not read Wi-Fi power saving state (%d).\n", ret);
         goto end;
      }
      if ((state = MIYOO_NLA_U32(a)) == NL80211_PS_DISABLED)
      {
         RARCH_LOG("[Netplay] Wi-Fi power saving already disabled.\n");
         goto end;
      }
      state = NL80211_PS_DISABLED;
   }
   else
      state = NL80211_PS_ENABLED;

   miyoo_nl_init(&req, family, NL80211_CMD_SET_POWER_SAVE);
   req.n.nlmsg_flags |= NLM_F_ACK;
   miyoo_nl_put(&req, NL80211_ATTR_IFINDEX, &ifindex, sizeof(ifindex));
   miyoo_nl_put(&req, NL80211_ATTR_PS_STATE, &state, sizeof(state));
   ret = miyoo_nl_transact(fd, &req, buf, sizeof(buf), &reply);

   if (netplay_active)
   {
      if (ret == 0)
      {
         miyoo_wifi_ps_restore = true;
         RARCH_LOG("[Netplay] Wi-Fi power saving disabled during netplay.\n");
      }
      else
         RARCH_WARN("[Netplay] Could not disable Wi-Fi power saving (%d).\n", ret);
   }
   else
   {
      if (ret == 0)
         RARCH_LOG("[Netplay] Wi-Fi power saving restored.\n");
      else
         RARCH_WARN("[Netplay] Could not restore Wi-Fi power saving (%d).\n", ret);
      miyoo_wifi_ps_restore = false;
   }

end:
   close(fd);
}

/* Mark netplay traffic as real time (DSCP EF): Wi-Fi drivers and WMM
 * routers then send it through the video access category, ahead of
 * bulk traffic. Failure is harmless. */
void netplay_miyoo_socket_lowlatency(int fd)
{
   int tos      = 0xB8; /* DSCP EF */
   int priority = 5;    /* 802.1d video, matches DSCP EF */

   setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
   setsockopt(fd, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));
}

/* True when there is a default route, i.e. the lobby and relay servers
 * can be reached. Without one (the Miyoo acting as the Wi-Fi access
 * point, or a router with no internet) every lobby request just fails,
 * so the caller can skip it. */
bool netplay_miyoo_has_internet(void)
{
   char line[256];
   bool found = false;
   FILE *f    = fopen("/proc/net/route", "r");

   if (!f)
      return true; /* Unknown: let RetroArch try */

   while (fgets(line, sizeof(line), f))
   {
      char iface[32];
      unsigned long dest, gateway;
      unsigned flags;

      if (     sscanf(line, "%31s %lx %lx %x", iface, &dest, &gateway, &flags) == 4
            && dest == 0
            && (flags & 0x1)) /* RTF_UP */
      {
         found = true;
         break;
      }
   }

   fclose(f);
   return found;
}
