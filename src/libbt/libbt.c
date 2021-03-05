#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "script.h"
#include "thread.h"
#include "sys.h"
#include "io.h"
#include "sim.h"
#include "debug.h"
#include "xalloc.h"

#define TAG_SCAN  "BT_SCAN"

// In L2CAP, ports are known as protocol service multiplexers (PSM) and take odd-numbered values between 1 to 32,767,
// where odd-numbered PSMs from 1 to 4,095 are reserved while odd-numbered PSMs from 4,097 to 32,765 are unreserved.

typedef struct {
  int pe;
  script_ref_t ref;
  int hdev, len;
} libbt_scan_t;

static bt_provider_t provider;

static char *get_minor_device_name(int major, int minor) {
  switch (major) {
  case 0:  // misc
    return "";
  case 1:  // computer
    switch (minor) {
      case 0: return "Uncategorized";
      case 1: return "Desktop workstation";
      case 2: return "Server";
      case 3: return "Laptop";
      case 4: return "Handheld";
      case 5: return "Palm";
      case 6: return "Wearable";
    }
    break;
  case 2:  // phone
    switch (minor) {
      case 0: return "Uncategorized";
      case 1: return "Cellular";
      case 2: return "Cordless";
      case 3: return "Smart phone";
      case 4: return "Wired modem or voice gateway";
      case 5: return "Common ISDN Access";
      case 6: return "Sim Card Reader";
    }
    break;
  case 3:  // lan access
    if (minor == 0) return "Uncategorized";
    switch (minor / 8) {
      case 0: return "Fully available";
      case 1: return "1-17% utilized";
      case 2: return "17-33% utilized";
      case 3: return "33-50% utilized";
      case 4: return "50-67% utilized";
      case 5: return "67-83% utilized";
      case 6: return "83-99% utilized";
      case 7: return "No service available";
    }
    break;
  case 4:  // audio/video
    switch (minor) {
      case 0: return "Uncategorized";
      case 1: return "Device conforms to the Headset profile";
      case 2: return "Hands-free";
      // 3 is reserved
      case 4: return "Microphone";
      case 5: return "Loudspeaker";
      case 6: return "Headphones";
      case 7: return "Portable Audio";
      case 8: return "Car Audio";
      case 9: return "Set-top box";
      case 10: return "HiFi Audio Device";
      case 11: return "VCR";
      case 12: return "Video Camera";
      case 13: return "Camcorder";
      case 14: return "Video Monitor";
      case 15: return "Video Display and Loudspeaker";
      case 16: return "Video Conferencing";
      // 17 is reserved
      case 18: return "Gaming/Toy";
    }
    break;
  case 5:  /* peripheral */ {
    static char cls_str[48];

    cls_str[0] = '\0';

    switch (minor & 48) {
      case 16: strncpy(cls_str, "Keyboard", sizeof(cls_str)); break;
      case 32: strncpy(cls_str, "Pointing device", sizeof(cls_str)); break;
      case 48: strncpy(cls_str, "Combo keyboard/pointing device", sizeof(cls_str)); break;
    }
    if ((minor & 15) && (strlen(cls_str) > 0)) strcat(cls_str, "/");

    switch (minor & 15) {
      case 0: break;
      case 1: strncat(cls_str, "Joystick", sizeof(cls_str) - strlen(cls_str)); break;
      case 2: strncat(cls_str, "Gamepad", sizeof(cls_str) - strlen(cls_str)); break;
      case 3: strncat(cls_str, "Remote control", sizeof(cls_str) - strlen(cls_str)); break;
      case 4: strncat(cls_str, "Sensing device", sizeof(cls_str) - strlen(cls_str)); break;
      case 5: strncat(cls_str, "Digitizer tablet", sizeof(cls_str) - strlen(cls_str)); break;
      case 6: strncat(cls_str, "Card reader", sizeof(cls_str) - strlen(cls_str)); break;
      default: strncat(cls_str, "(reserved)", sizeof(cls_str) - strlen(cls_str)); break;
    }
    if (strlen(cls_str) > 0) return cls_str;
  }
  case 6:  // imaging
    if (minor & 4) return "Display";
    if (minor & 8) return "Camera";
    if (minor & 16) return "Scanner";
    if (minor & 32) return "Printer";
    break;
  case 7: // wearable
    switch (minor) {
      case 1: return "Wrist Watch";
      case 2: return "Pager";
      case 3: return "Jacket";
      case 4: return "Helmet";
      case 5: return "Glasses";
    }
    break;
  case 8: // toy
    switch (minor) {
      case 1: return "Robot";
      case 2: return "Vehicle";
      case 3: return "Doll / Action Figure";
      case 4: return "Controller";
      case 5: return "Game";
    }
    break;
  case 63:  // uncategorised
    return "";
  }
  return "Unknown (reserved) minor device class";
}

static void print_dev_hdr(struct hci_dev_info *di) {
  char addr[32];

  if (di->dev_id != -1) {
    ba2str(&di->bdaddr, addr);
    debug(DEBUG_INFO, "BT", "Name %s", di->name);
    debug(DEBUG_INFO, "BT", "Type %s", hci_typetostr((di->type & 0x30) >> 4));
    debug(DEBUG_INFO, "BT", "Bus  %s", hci_bustostr(di->type & 0x0f));
    debug(DEBUG_INFO, "BT", "BD Address %s", addr);
    debug(DEBUG_INFO, "BT", "ACL MTU %d:%d", di->acl_mtu, di->acl_pkts);
    debug(DEBUG_INFO, "BT", "SCO MTU %d:%d", di->sco_mtu, di->sco_pkts);
  }
}

static void print_dev_features(struct hci_dev_info *di, int format) {
  debug(DEBUG_INFO, "BT", "Features 0x%2.2x 0x%2.2x 0x%2.2x 0x%2.2x 0x%2.2x 0x%2.2x 0x%2.2x 0x%2.2x",
    di->features[0], di->features[1], di->features[2],
    di->features[3], di->features[4], di->features[5],
    di->features[6], di->features[7]);

  if (format) {
    char *tmp = lmp_featurestostr(di->features, "  ", 63);
    debug(DEBUG_INFO, "BT", "%s", tmp);
    bt_free(tmp);
  }
}

static void print_pkt_type(struct hci_dev_info *di) {
  char *str;

  if ((str = hci_ptypetostr(di->pkt_type)) != NULL) {
    debug(DEBUG_INFO, "BT", "Packet type %s", str);
    bt_free(str);
  }
}

static void print_link_policy(struct hci_dev_info *di) {
  debug(DEBUG_INFO, "BT", "Link policy %s", hci_lptostr(di->link_policy));
}

static void print_link_mode(struct hci_dev_info *di) {
  char *str;

  if ((str = hci_lmtostr(di->link_mode)) != NULL) {
    debug(DEBUG_INFO, "BT", "Link mode %s", str);
    bt_free(str);
  }
}

static int cmd_name(int ctl, int hdev, char *opt, struct hci_dev_info *di) {
  char name[249];
  int i, dd;

  if ((dd = hci_open_dev(hdev)) < 0) {
    debug_errno("BT", "hci_open_dev %d", hdev);
    return -1;
  }

  if (opt) {
    if (hci_write_local_name(dd, opt, 2000) < 0) {
      debug_errno("BT", "hci_write_local_name %d", hdev);
      hci_close_dev(dd);
      return -1;
    }

  } else {
    if (hci_read_local_name(dd, sizeof(name), name, 1000) < 0) {
      debug_errno("BT", "hci_read_local_name %d", hdev);
      hci_close_dev(dd);
      return -1;
    }

    for (i = 0; i < 248 && name[i]; i++) {
      if ((unsigned char) name[i] < 32 || name[i] == 127) {
        name[i] = '.';
      }
    }

    name[248] = '\0';
    debug(DEBUG_INFO, "BT", "Local name %s", name);
  }

  hci_close_dev(dd);
  return 0;
}

static int cmd_class(int ctl, int hdev, char *opt, struct hci_dev_info *di) {
  static const char *services[] = {
    "Positioning", "Networking", "Rendering", "Capturing", "Object Transfer", "Audio", "Telephony", "Information"
  };
  static const char *major_devices[] = {
    "Miscellaneous", "Computer", "Phone", "LAN Access", "Audio/Video", "Peripheral", "Imaging", "Uncategorized"
  };

  unsigned int i;
  int dd;

  if ((dd = hci_open_dev(hdev)) < 0) {
    debug_errno("BT", "hci_open_dev %d", hdev);
    return -1;
  }
  if (opt) {
    uint32_t cod = strtoul(opt, NULL, 16);
    if (hci_write_class_of_dev(dd, cod, 2000) < 0) {
      debug_errno("BT", "hci_write_class_of_dev %d", hdev);
      hci_close_dev(dd);
      return -1;
    }
  } else {
    uint8_t cls[3];
    if (hci_read_class_of_dev(dd, cls, 1000) < 0) {
      debug_errno("BT", "hci_read_class_of_dev %d", hdev);
      hci_close_dev(dd);
      return -1;
    }
    debug(DEBUG_INFO, "BT", "Class 0x%02x%02x%02x\n", cls[2], cls[1], cls[0]);

    if (cls[2]) {
      debug(DEBUG_INFO, "BT", "Service Classes");
      debug_indent(2);
      for (i = 0; i < (sizeof(services) / sizeof(*services)); i++) {
        if (cls[2] & (1 << i)) {
          debug(DEBUG_INFO, "BT", "%s", services[i]);
        }
      }
      debug_indent(-2);
    } else {
      debug(DEBUG_INFO, "BT", "Unspecified Service Class");
    }

    if ((cls[1] & 0x1f) >= sizeof(major_devices) / sizeof(*major_devices)) {
      debug(DEBUG_INFO, "BT", "Invalid Device Class 0x%02X", cls[1] & 0x1f);
    } else {
      debug(DEBUG_INFO, "BT", "Device Class %s, %s", major_devices[cls[1] & 0x1f], get_minor_device_name(cls[1] & 0x1f, cls[0] >> 2));
    }
  }

  hci_close_dev(dd);
  return 0;
}

static int cmd_version(int ctl, int hdev, char *opt, struct hci_dev_info *di) {
  struct hci_version ver;
  char *hciver = NULL, *lmpver = NULL;
  int dd;

  if ((dd = hci_open_dev(hdev)) < 0) {
    debug_errno("BT", "hci_open_dev %d", hdev);
    return -1;
  }

  if (hci_read_local_version(dd, &ver, 1000) < 0) {
    debug_errno("BT", "hci_read_local_version %d", hdev);
    hci_close_dev(dd);
    return -1;
  }

  hciver = hci_vertostr(ver.hci_ver);
  if (((di->type & 0x30) >> 4) == HCI_BREDR) {
    lmpver = lmp_vertostr(ver.lmp_ver);
  } else {
    lmpver = pal_vertostr(ver.lmp_ver);  // XXX does not exist on wheezy
  }

  debug(DEBUG_INFO, "BT", "HCI Version %s (0x%X)", hciver ? hciver : "n/a", ver.hci_rev);
  debug(DEBUG_INFO, "BT", "Revision 0x%X", ver.hci_rev);
  debug(DEBUG_INFO, "BT", "%s Version %s (0x%X)", (((di->type & 0x30) >> 4) == HCI_BREDR) ? "LMP" : "PAL", lmpver ? lmpver : "n/a", ver.lmp_ver);
  debug(DEBUG_INFO, "BT", "Subversion 0x%X", ver.lmp_subver);
  debug(DEBUG_INFO, "BT", "Manufacturer %s (%d)", bt_compidtostr(ver.manufacturer), ver.manufacturer);

  if (hciver) bt_free(hciver);
  if (lmpver) bt_free(lmpver);

  hci_close_dev(dd);
  return 0;
}

static void print_dev_info(int sock, struct hci_dev_info *di) {
  struct hci_dev_stats *st = &di->stat;
  char *str;

  print_dev_hdr(di);

  str = hci_dflagstostr(di->flags);
  debug(DEBUG_INFO, "BT", "Flags %s", str);
  bt_free(str);

  debug(DEBUG_INFO, "BT", "RX bytes:%d acl:%d sco:%d events:%d errors:%d", st->byte_rx, st->acl_rx, st->sco_rx, st->evt_rx, st->err_rx);
  debug(DEBUG_INFO, "BT", "TX bytes:%d acl:%d sco:%d commands:%d errors:%d", st->byte_tx, st->acl_tx, st->sco_tx, st->cmd_tx, st->err_tx);

  if (!hci_test_bit(HCI_RAW, &di->flags)) {
    print_dev_features(di, 0);

    if (((di->type & 0x30) >> 4) == HCI_BREDR) {
      print_pkt_type(di);
      print_link_policy(di);
      print_link_mode(di);

      if (hci_test_bit(HCI_UP, &di->flags)) {
        cmd_name(sock, di->dev_id, NULL, di);
        cmd_class(sock, di->dev_id, NULL, di);
      }
    }

    if (hci_test_bit(HCI_UP, &di->flags)) {
      cmd_version(sock, di->dev_id, NULL, di);
    }
  }
}

static int libbt_list(int pe) {
  struct hci_dev_list_req *dl;
  struct hci_dev_req *dr;
  struct hci_dev_info di;
  script_arg_t key, value;
  script_ref_t obj;
  int sock, dd, i, j, r = -1;

  if ((sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) != -1) {
    if ((dl = xmalloc(HCI_MAX_DEV * sizeof(struct hci_dev_req) + sizeof(uint16_t))) != NULL) {
      dl->dev_num = HCI_MAX_DEV;
      dr = dl->dev_req;

      if (ioctl(sock, HCIGETDEVLIST, (void *)dl) != -1) {
        debug(DEBUG_INFO, "BT", "Found %d interface(s)", dl->dev_num);
        debug_indent(2);
        obj = script_create_object(pe);

        for (i = 0, j = 1; i < dl->dev_num; i++) {
          di.dev_id = (dr + i)->dev_id;
          if (ioctl(sock, HCIGETDEVINFO, (void *)&di) < 0) continue;
          if (hci_test_bit(HCI_RAW, &di.flags) && !bacmp(&di.bdaddr, BDADDR_ANY)) {
            dd = hci_open_dev(di.dev_id);
            hci_read_bd_addr(dd, &di.bdaddr, 1000);
            hci_close_dev(dd);
          }
          debug(DEBUG_INFO, "BT", "Interface %d", di.dev_id);
          debug_indent(2);
          print_dev_info(sock, &di);
          debug_indent(-2);

          key.type = SCRIPT_ARG_INTEGER;
          key.value.i = j++;
          value.type = SCRIPT_ARG_INTEGER;
          value.value.i = di.dev_id;
          script_object_set(pe, obj, &key, &value);
        }
        debug_indent(-2);

        r = script_push_object(pe, obj);
        script_remove_ref(pe, obj);

      } else {
        debug_errno("BT", "ioctl HCIGETDEVLIST");
      }
      xfree(dl);
    }
    close(sock);
  } else {
    debug_errno("BT", "socket");
  }

  return r;
}

static int libbt_scan_action(void *arg) {
  libbt_scan_t *scan;
  script_arg_t ret;
  uint8_t lap[3] = { 0x33, 0x8b, 0x9e };
  char addr[18], name[249];
  inquiry_info *info;
  int dd, num_rsp, flags, i, n;

  scan = (libbt_scan_t *)arg;
  info = NULL;
  num_rsp = 0;
  flags = 0;

  debug(DEBUG_INFO, "BT", "Scanning for %0.2fs ...", scan->len*1.28);
  num_rsp = hci_inquiry(scan->hdev, scan->len, num_rsp, lap, &info, flags);

  if (num_rsp >= 0) {
    if ((dd = hci_open_dev(scan->hdev)) != -1) {
      debug(DEBUG_INFO, "BT", "Found %d device(s)", num_rsp);

      for (i = 0; i < num_rsp; i++) {
        ba2str(&(info+i)->bdaddr, addr);

        if (hci_read_remote_name_with_clock_offset(dd, &(info+i)->bdaddr, (info+i)->pscan_rep_mode, (info+i)->clock_offset | 0x8000, sizeof(name), name, 100000) < 0) {
          strcpy(name, "n/a");
        }
        for (n = 0; n < 248 && name[n]; n++) {
          if ((unsigned char) name[i] < 32 || name[i] == 127) name[i] = '.';
        }
        name[248] = '\0';
        debug(DEBUG_INFO, "BT", "  Device %s \"%s\"", addr, name);
        script_call(scan->pe, scan->ref, &ret, "SS", addr, name);
      }
      hci_close_dev(dd);
      script_call(scan->pe, scan->ref, &ret, "");

    } else {
      debug_errno("BT", "hci_open_dev %d", scan->hdev);
    }
  } else {
    debug_errno("BT", "hci_inquiry");
  }

  script_remove_ref(scan->pe, scan->ref);
  thread_end(TAG_SCAN, thread_get_handle());
  xfree(scan);

  return 0;
}

static int libbt_scan(int pe) {
  script_ref_t ref;
  script_int_t hdev, len;
  libbt_scan_t *scan;
  int handle, r = -1;

  if (script_get_function(pe, 0, &ref) == 0 &&
      script_get_integer(pe, 1, &hdev) == 0 &&
      script_get_integer(pe, 2, &len) == 0) {

    if ((scan = xcalloc(1, sizeof(libbt_scan_t))) != NULL) {
      scan->pe = pe;
      scan->ref = ref;
      scan->hdev = hdev;
      scan->len = len;

      if ((handle = thread_begin(TAG_SCAN, libbt_scan_action, scan)) == -1) {
        xfree(scan);
      } else {
        r = script_push_integer(pe, handle);
      }
    }
  }

  return r;
}


static int libbt_addr(int pe) {
  script_int_t hdev;
  struct hci_dev_info di;
  char addr[32];
  int sock, dd, r = -1;

  if (script_get_integer(pe, 0, &hdev) == 0) {
    if ((sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) != -1) {
      di.dev_id = hdev;
      if (ioctl(sock, HCIGETDEVINFO, (void *)&di) != -1) {
        if (hci_test_bit(HCI_RAW, &di.flags) && !bacmp(&di.bdaddr, BDADDR_ANY)) {
          dd = hci_open_dev(di.dev_id);
          hci_read_bd_addr(dd, &di.bdaddr, 1000);
          hci_close_dev(dd);
        }
        memset(addr, 0, sizeof(addr));
        ba2str(&di.bdaddr, addr);
        if (addr[0]) {
          r = script_push_string(pe, addr);
        }
      }
      close(sock);
    } else {
      debug_errno("BT", "socket");
    }
  }

  return r;
}

static int libbt_cmd_isup(int pe) {
  script_int_t hdev;
  struct hci_dev_info di;
  int sock, r = -1;

  if (script_get_integer(pe, 0, &hdev) == 0) {
    if ((sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) != -1) {
      di.dev_id = hdev;
      if (ioctl(sock, HCIGETDEVINFO, (void *)&di) != -1) {
        if (hci_test_bit(HCI_UP, &di.flags)) {
          r = 0;
        }
      }
      close(sock);
    } else {
      debug_errno("BT", "socket");
    }
  }

  return script_push_boolean(pe, r == 0);
}

static int libbt_cmd_updown(int pe) {
  script_int_t hdev;
  int sock, up, cmd, r = -1;

  if (script_get_integer(pe, 0, &hdev) == 0 &&
      script_get_boolean(pe, 1, &up) == 0) {

    if ((sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) != -1) {
      cmd = up ? HCIDEVUP : HCIDEVDOWN;
      if ((r = ioctl(sock, cmd, hdev)) < 0) {
        if (up && errno == EALREADY) {
          r = 0;
        } else {
          debug_errno("BT", "ioctl %s", up ? "HCIDEVUP" : "HCIDEVDOWN");
        }
      }
      close(sock);
    } else {
      debug_errno("BT", "socket");
    }
  }

  return script_push_boolean(pe, r == 0);
}

static int libbt_cmd_auth(int pe) {
  script_int_t hdev;
  struct hci_dev_req dr;
  int sock, auth, r = -1;

  if (script_get_integer(pe, 0, &hdev) == 0 &&
      script_get_boolean(pe, 1, &auth) == 0) {

    if ((sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) != -1) {
      dr.dev_id = hdev;
      dr.dev_opt = auth ? AUTH_ENABLED : AUTH_DISABLED;
      if ((r = ioctl(sock, HCISETAUTH, (unsigned long) &dr)) < 0) {
        debug_errno("BT", "ioctl HCISETAUTH");
      }
      close(sock);
    } else {
      debug_errno("BT", "socket");
    }
  }

  return script_push_boolean(pe, r == 0);
}

// L2CAP: only odd numbers in 0x1001 .. 0x10FF ???

#define PROTOCOLS \
  switch (type) { \
    case BT_RFCOMM: \
      stype = SOCK_STREAM; \
      protocol = BTPROTO_RFCOMM; \
      memset(&s_addr, 0, sizeof(s_addr)); \
      s_addr.rc_family = AF_BLUETOOTH; \
      str2ba(addr, &s_addr.rc_bdaddr); \
      s_addr.rc_channel = port; \
      addrlen = sizeof(s_addr); \
      bt_addr = (struct sockaddr *)&s_addr; \
      break; \
    case BT_L2CAP: \
      stype = SOCK_SEQPACKET; \
      protocol = BTPROTO_L2CAP; \
      memset(&d_addr, 0, sizeof(d_addr)); \
      d_addr.l2_family = AF_BLUETOOTH; \
      str2ba(addr, &d_addr.l2_bdaddr); \
      d_addr.l2_psm = htobs(port); \
      addrlen = sizeof(d_addr); \
      bt_addr = (struct sockaddr *)&d_addr; \
      break; \
    default: \
      debug(DEBUG_ERROR, "BT", "invalid type"); \
      return -1; \
  }

static int bt_bind(char *addr, int port, int type) {
  struct sockaddr_rc s_addr;
  struct sockaddr_l2 d_addr;
  struct sockaddr *bt_addr;
  socklen_t addrlen;
  char baddr[32];
  int stype, sock, protocol, r;

  PROTOCOLS;

  debug(DEBUG_INFO, "BT", "trying to bind to %s addr %s port %d", type == BT_RFCOMM ? "RFCOMM" : "L2CAP", addr, port);

  if ((sock = socket(AF_BLUETOOTH, stype, protocol)) == -1) {
    debug_errno("BT", "socket");
    return -1;
  }

  if (bind(sock, bt_addr, addrlen) != 0) {
    debug_errno("BT", "bind to channel %d", port);
    close(sock);
    return -1;
  }

  switch (type) {
    case BT_RFCOMM:
      memset(&s_addr, 0, sizeof(s_addr));
      r = getsockname(sock, (struct sockaddr *)&s_addr, &addrlen);
      ba2str(&s_addr.rc_bdaddr, baddr);
      port = s_addr.rc_channel;
      break;
    case BT_L2CAP:
      memset(&d_addr, 0, sizeof(d_addr));
      r = getsockname(sock, (struct sockaddr *)&d_addr, &addrlen);
      ba2str(&d_addr.l2_bdaddr, baddr);
      port = btohs(d_addr.l2_psm);
      break;
    default:
      r = -1;
      port = -1;
      break;
  }

  if (r != 0) {
    debug_errno("BT", "getsockname");
    close(sock);
    return -1;
  }

  if (listen(sock, 5) != 0) {
    debug_errno("BT", "listen");
    close(sock);
    return -1;
  }

  debug(DEBUG_INFO, "BT", "fd %d bound to %s addr %s port %d", sock, type == BT_RFCOMM ? "RFCOMM" : "L2CAP", baddr, port);

  return sock;
}

static int bt_accept(int sock, char *addr, int *port, int type, struct timeval *tv) {
  struct sockaddr_rc s_addr;
  struct sockaddr_l2 d_addr;
  struct sockaddr *bt_addr;
  socklen_t addrlen;
  int csock, r;

  if ((r = sys_select(sock, tv ? (tv->tv_sec * 1000000 + tv->tv_usec) : -1)) <= 0) {
    return r;
  }

  switch (type) {
    case BT_RFCOMM:
      memset(&s_addr, 0, sizeof(s_addr));
      addrlen = sizeof(s_addr);
      bt_addr = (struct sockaddr *)&s_addr;
      break;
    case BT_L2CAP:
      memset(&d_addr, 0, sizeof(d_addr));
      addrlen = sizeof(d_addr);
      bt_addr = (struct sockaddr *)&d_addr;
      break;
    default:
      debug(DEBUG_ERROR, "BT", "invalid type");
      return -1;
  }

  csock = accept(sock, bt_addr, &addrlen);

  if (csock == -1) {
    debug_errno("BT", "accept");
    return -1;
  }

  switch (type) {
    case BT_RFCOMM:
      ba2str(&s_addr.rc_bdaddr, addr);
      *port = s_addr.rc_channel;
      break;
    case BT_L2CAP:
      ba2str(&d_addr.l2_bdaddr, addr);
      *port = btohs(d_addr.l2_psm);
      break;
  }

  debug(DEBUG_INFO, "BT", "fd %d accepted from %s addr %s port %d", csock, type == BT_RFCOMM ? "RFCOMM" : "L2CAP", addr, *port);

  return csock;
}

static int bt_connect(char *addr, int port, int type) {
  struct sockaddr_rc s_addr;
  struct sockaddr_l2 d_addr;
  struct sockaddr *bt_addr;
  socklen_t addrlen;
  int stype, protocol, sock;

  debug(DEBUG_INFO, "BT", "trying to connect to %s addr %s port %d", type == BT_RFCOMM ? "RFCOMM" : "L2CAP", addr, port);

  if ((sock = sim_connect(addr, port)) == -1) {
    return -1;
  }

  if (sock) {
    return sock;
  }

  PROTOCOLS;

  if ((sock = socket(AF_BLUETOOTH, stype, protocol)) == -1) {
    debug_errno("BT", "socket");
    return -1;
  }

  if (connect(sock, bt_addr, addrlen) == -1) {
    debug_errno("BT", "connect to %s channel %d", addr, port);
    close(sock);
    return -1;
  }

  debug(DEBUG_INFO, "BT", "fd %d connected to %s addr %s port %d", sock, type == BT_RFCOMM ? "RFCOMM" : "L2CAP", addr, port);

  return sock;
}

int libbt_load(void) {
  provider.bind = bt_bind;
  provider.accept = bt_accept;
  provider.connect = bt_connect;

  return 0;
}

int libbt_init(int pe, script_ref_t obj) {
  debug(DEBUG_INFO, "BT", "registering %s", BT_PROVIDER);
  script_set_pointer(pe, BT_PROVIDER, &provider);

  script_add_function(pe, obj, "list", libbt_list);
  script_add_function(pe, obj, "scan", libbt_scan);
  script_add_function(pe, obj, "addr", libbt_addr);
  script_add_function(pe, obj, "isup", libbt_cmd_isup);
  script_add_function(pe, obj, "up",   libbt_cmd_updown);
  script_add_function(pe, obj, "auth", libbt_cmd_auth);

  return 0;
}
