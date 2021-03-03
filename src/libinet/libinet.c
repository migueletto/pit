#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>

#include "script.h"
#include "xalloc.h"
#include "debug.h"

static int getip(char *interface, int family, int addrlen, char *host, int hlen, int broadcast) {
  struct ifaddrs *ifaddr, *ifa;
  int r = -1;

  if (getifaddrs(&ifaddr) == -1) {
    debug_errno("INET", "getifaddrs");
    return -1;
  }

  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) continue;  

    if (!strcmp(ifa->ifa_name, interface)) {
      debug(DEBUG_TRACE, "INET", "family=%d, flags=%08x", ifa->ifa_addr->sa_family, ifa->ifa_flags);
      if (ifa->ifa_addr->sa_family == family) {
        if ((ifa->ifa_flags & IFF_UP) && (ifa->ifa_flags & IFF_RUNNING)) {
          r = getnameinfo(broadcast ? ifa->ifa_broadaddr : ifa->ifa_addr, addrlen, host, hlen, NULL, 0, NI_NUMERICHOST);
          if (r == 0) {
            break;
          } else {
            debug(DEBUG_ERROR, "INET", "getnameinfo failed: %s", gai_strerror(r));
          }
        } else {
          debug(DEBUG_INFO, "INET", "interface %s is not UP/RUNNING", interface);
        }
      }
    }
  }

  freeifaddrs(ifaddr);
  return r;
}

static int libinet_list(int pe) {
  script_ref_t obj;
  script_arg_t key;
  script_arg_t value;
  struct ifaddrs *ifaddr, *ifa;
  int r, i;

  if (getifaddrs(&ifaddr) == -1) {
    debug_errno("INET", "getifaddrs");
    return -1;
  }

  obj = script_create_object(pe);

  for (ifa = ifaddr, i = 1; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) continue;

    if (ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family == AF_INET6) {
      key.type = SCRIPT_ARG_STRING;
      key.value.s = ifa->ifa_name;
      r = script_object_get(pe, obj, &key, &value);
      if (r != 0 || value.type == SCRIPT_ARG_NULL) {
        value.type = SCRIPT_ARG_STRING;
        value.value.s = ifa->ifa_name;
        script_object_set(pe, obj, &key, &value);
        key.type = SCRIPT_ARG_INTEGER;
        key.value.i = i++;
        script_object_set(pe, obj, &key, &value);
      }
    }
  }

  r = script_push_object(pe, obj);
  script_remove_ref(pe, obj);

  return r;
}

static int libinet_ip(int pe) {
  char *interface = NULL;
  char host[NI_MAXHOST];
  int r = -1;

  if (script_get_string(pe, 0, &interface) == 0) {
    if (getip(interface, AF_INET, sizeof(struct sockaddr_in), host, NI_MAXHOST, 0) == 0) {
      r = script_push_string(pe, host);
    }
  }

  if (interface) xfree(interface);

  return r;
}

static int libinet_broadcast(int pe) {
  char *interface = NULL;
  char host[NI_MAXHOST];
  int r = -1;

  if (script_get_string(pe, 0, &interface) == 0) {
    if (getip(interface, AF_INET, sizeof(struct sockaddr_in), host, NI_MAXHOST, 1) == 0) {
      r = script_push_string(pe, host);
    }
  }

  if (interface) xfree(interface);

  return r;
}

static int libinet_ipv6(int pe) {
  char *interface = NULL;
  char host[NI_MAXHOST];
  int r = -1;

  if (script_get_string(pe, 0, &interface) == 0) {
    if (getip(interface, AF_INET6, sizeof(struct sockaddr_in6), host, NI_MAXHOST, 0) == 0) {
      r = script_push_string(pe, host);
    }
  }

  if (interface) xfree(interface);

  return r;
}

int libinet_init(int pe, script_ref_t obj) {
  script_add_function(pe, obj, "list", libinet_list);
  script_add_function(pe, obj, "ip", libinet_ip);
  script_add_function(pe, obj, "broadcast", libinet_broadcast);
  script_add_function(pe, obj, "ipv6", libinet_ipv6);

  return 0;
}
