#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "script.h"
#include "thread.h"
#include "media.h"
#include "bytes.h"
#include "sys.h"
#include "io.h"
#include "rtpnode.h"
#include "debug.h"
#include "xalloc.h"

#define RTP_PACKET 65000

typedef struct {
  int first;
  int rtp_sock, peer_rtp_port;
  int rtcp_sock, peer_rtcp_port;
  char peer_host[MAX_HOST];
  uint16_t sequence;
  uint32_t ssrc, clockrate;
  int64_t t0;
  uint8_t packet[RTP_PACKET];
} rtp_node_t;

static const char *sdes[] = { "END", "CNAME", "NAME", "EMAIL", "PHONE", "LOC", "TOOL", "NOTE", "PRIV" };

static int libmedia_rtp_process(media_frame_t *frame, void *data);
static int libmedia_rtp_option(char *name, char *value, void *data);
static int libmedia_rtp_destroy(void *data);

static node_dispatch_t rtp_dispatch = {
  libmedia_rtp_process,
  libmedia_rtp_option,
  NULL,
  libmedia_rtp_destroy
};

static void send_rtp_frame(int encoding, int width, int height, uint8_t *buf, int len, int64_t ts, rtp_node_t *data) {
  uint32_t offset, flen, i;
  uint64_t timestamp;

  if (buf && len > 0 && data->rtp_sock > 0 && data->peer_rtp_port > 0 && data->peer_host[0]) {
    if (data->first) {
      data->t0 = ts;
      data->first = 0;
      timestamp = 0;
    } else {
      timestamp = ((uint64_t)data->clockrate * (uint64_t)(ts - data->t0)) / 1000000;
    }

    // RTP header
    data->packet[0] = 0x80;                   // version 2, no padding, no extension, no CSRC identifiers
    put2b(data->sequence++, data->packet, 2); // sequence number
    put4b(timestamp, data->packet, 4);        // timestamp
    put4b(data->ssrc, data->packet, 8);       // SSRC identifier

    switch (encoding) {
      case ENC_JPEG:
        data->packet[1] = 26; // payload type = JPEG

        // JPEG payload header
        //  0                   1                   2                   3
        //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        // | Type-specific |              Fragment Offset                  |
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        // |      Type     |       Q       |     Width     |     Height    |
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

        data->packet[16] = 1;              // type 1: chrominance components are downsampled both horizontally and vertically by 2 (4:2:0)
        data->packet[17] = 1;              // 1 <= Q <= 50: scale = 5000 / Q; 51 <= Q <= 99: scale = 200 - 2 * Q
        data->packet[18] = width >> 3;     // width
        data->packet[19] = height >> 3;    // height

        for (offset = 0, i = 0; len > 0; i++) {
          put4b(offset, data->packet, 12); // fragment offset (MSB is overwritten below)
          data->packet[12] = 0;            // type-specific 0: image is progressively scanned

          if (len <= RTP_PACKET - 20) {
            data->packet[1] |= 0x80;       // M=1 (frame is complete)
            flen = len;
          } else {
            flen = RTP_PACKET - 20;
          }

          xmemcpy(&data->packet[20], &buf[offset], flen);
          debug(DEBUG_TRACE, "RTP", "send JPEG seq %d ts %u to host %s port %d", data->sequence, timestamp, data->peer_host, data->peer_rtp_port);
          sys_socket_sendto(data->rtp_sock, data->peer_host, data->peer_rtp_port, data->packet, 20 + flen);
          offset += flen;
          len -= flen;
        }
        break;
      default:
        debug(DEBUG_ERROR, "RTP", "unsupported video type %s", video_encoding_name(encoding));
        break;
    }
  }
}

static int recv_rtp_packet(uint8_t *packet, int n, rtp_node_t *data) {
  debug_bytes(DEBUG_TRACE, "RTP", packet, n);
  return 0;
}

static int recv_rtcp_packet(uint8_t *packet, int n, rtp_node_t *data) {
  uint8_t rc, pt, it, ilen;
  uint16_t len;
  uint32_t ssrc, aux;
  int i, j;

  debug_bytes(DEBUG_TRACE, "RTP", packet, n);

  for (j = 0; j < n;) {
    if ((packet[j] & 0xC0) != 0x80) break;
    rc = packet[j++] & 0x1F;
    pt = packet[j++];
    j += get2b(&len, packet, j);

    switch (pt) {
      case 200: // sender report
        debug(DEBUG_INFO, "RTP", "RTCP sender report");
        j = n;
        break;
      case 201: // receiver report
        j += get4b(&ssrc, packet, j);
        debug(DEBUG_INFO, "RTP", "RTCP receiver report from 0x%08x", ssrc);
        for (i = 0; i < rc; i++) {
          j += get4b(&aux,  packet, j);
          debug(DEBUG_INFO, "RTP", "RTCP report for source 0x%08x", aux);
          j += get4b(&aux,  packet, j);
          debug(DEBUG_INFO, "RTP", "RTCP report: %d packets lost", aux & 0xffffff);
          j += get4b(&aux,  packet, j);
          debug(DEBUG_INFO, "RTP", "RTCP report: highest sequence %d %d", aux >> 16, aux & 0xffff);
          j += get4b(&aux,  packet, j);
          debug(DEBUG_INFO, "RTP", "RTCP report: jitter %d", aux);
          j += get4b(&aux,  packet, j);
          debug(DEBUG_INFO, "RTP", "RTCP report: last sr %d", aux);
          j += get4b(&aux,  packet, j);
          debug(DEBUG_INFO, "RTP", "RTCP report: delay since last sr %d", aux);
        }
        break;
      case 202: // source description
        for (i = 0; i < rc; i++) {
          j += get4b(&ssrc, packet, j);
          debug(DEBUG_INFO, "RTP", "RTCP source description from 0x%08x", ssrc);
          for (;;) {
            it = packet[j++];
            if (it == 0) break;
            ilen = packet[j++];
            debug(DEBUG_INFO, "RTP", "RTCP item type %d %s \"%.*s\"", it, (it <= 8) ? sdes[it] : "???", ilen, (char *)&packet[j]);
            j += ilen;
          }
          if (j % 4) j += 4 - (j % 4);
        }
        break;
      case 203: // bye
        for (i = 0; i < rc; i++) {
          j += get4b(&ssrc, packet, j);
          debug(DEBUG_INFO, "RTP", "RTCP bye from 0x%08x", ssrc);
        }
        j = n;
        break;
      case 204: // app defined
        debug(DEBUG_INFO, "RTP", "RTCP app defined");
        j = n;
        break;
    }
  }

  return 0;
}

static int send_rtcp_report(rtp_node_t *data) {
  int j = 0;

  if (data->peer_rtcp_port) {
    // convert from UNIX time to NTP:
    // Unix uses an epoch of 1/1/1970-00:00h (UTC) and NTP uses 1/1/1900-00:00h.
    // This leads to an offset equivalent to 70 years in seconds (there are 17 leap years between the two dates so the offset is: (70*365 + 17)*86400 = 2208988800
    // To convert from struct timeval copy the tv_usec field of the unix time to a uint64_t and left shift it 32 bit positions,
    // then divide it by 1000000 and convert to network byte order (most significative byte first) 
/*
    // Sender Report
    // XXX causes VLC to freeze playback with warning "early picture received"
    data->packet[j++] = 0x81;
    data->packet[j++] = 200; // source description
    j += put2b(13-1, data->packet, j); // 13 (32-bits words) - 1
    j += put4b(data->ssrc, data->packet, j);
    j += put4b(0, data->packet, j); // NTP high
    j += put4b(0, data->packet, j); // NTP low
    j += put4b(0, data->packet, j); // RTP
    j += put4b(0, data->packet, j); // packet count
    j += put4b(0, data->packet, j); // octet count
    j += put4b(data->ssrc, data->packet, j);
    j += put4b(0x00ffffff, data->packet, j); // packet loss
    j += put4b(0, data->packet, j); // highest sequence
    j += put4b(0, data->packet, j); // jitter
    j += put4b(0, data->packet, j); // lsr
    j += put4b(0, data->packet, j); // dlsr
*/

    // Source Description
    data->packet[j++] = 0x81;
    data->packet[j++] = 202; // source description
    j += put2b(3-1, data->packet, j); // 3 (32-bits words) - 1
    j += put4b(data->ssrc, data->packet, j);
    data->packet[j++] = 1;
    data->packet[j++] = 2;
    data->packet[j++] = 'm';
    data->packet[j++] = 'm';

    debug(DEBUG_INFO, "RTP", "send %d bytes to host %s port %d", j, data->peer_host, data->peer_rtcp_port);
    sys_socket_sendto(data->rtcp_sock, data->peer_host, data->peer_rtcp_port, data->packet, j);
    debug_bytes(DEBUG_TRACE, "RTP", data->packet, j);
  }

  return 0;
}

static int libmedia_rtp_process(media_frame_t *frame, void *_data) {
  rtp_node_t *data;
  struct timeval tv;
  char host[MAX_HOST];
  int port, n;

  data = (rtp_node_t *)_data;
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  if (data->rtp_sock > 0) {
    if ((n = sys_socket_recvfrom(data->rtp_sock, host, sizeof(host)-1, &port, data->packet, RTP_PACKET, &tv)) > 0) {
      debug(DEBUG_INFO, "RTP", "received %d bytes from host %s port %d on RTP port", n, host, port);
      recv_rtp_packet(data->packet, n, data);
    }
  }

  if (data->rtcp_sock > 0) {
    if ((n = sys_socket_recvfrom(data->rtcp_sock, host, sizeof(host)-1, &port, data->packet, RTP_PACKET, &tv)) > 0) {
      debug(DEBUG_INFO, "RTP", "received %d bytes from host %s port %d on RTCP port", n, host, port);
      recv_rtcp_packet(data->packet, n, data);
    }
  }

  if (frame->len == 0) {
    return 0;
  }

  if (frame->meta.type != FRAME_TYPE_VIDEO) {
    return 1;
  }

  send_rtp_frame(frame->meta.av.v.encoding, frame->meta.av.v.width, frame->meta.av.v.height, frame->frame, frame->len, frame->ts, data);

  return 1;
}

static int libmedia_rtp_option(char *name, char *value, void *_data) {
  rtp_node_t *data;
  int r = -1;

  data = (rtp_node_t *)_data;
  debug(DEBUG_INFO, "RTP", "set option \"%s\" to \"%s\"", name, value ? value : "(null)");

  if (!strcmp(name, "port")) {
    data->peer_rtp_port = value && value[0] ? atoi(value) : 0;
    data->peer_rtcp_port = data->peer_rtp_port ? data->peer_rtp_port+1 : 0;
    send_rtcp_report(data);
    data->first = 1;
    r = 0;
  } else if (!strcmp(name, "host")) {
    if (value && value[0]) {
      xmemcpy(data->peer_host, value, MAX_HOST-1);
    } else {
      xmemset(data->peer_host, 0, MAX_HOST);
    }
    r = 0;
  }

  return r;
}

static int libmedia_rtp_destroy(void *_data) {
  rtp_node_t *data;

  data = (rtp_node_t *)_data;
  if (data->rtp_sock > 0) {
    sys_close(data->rtp_sock);
  }
  if (data->rtcp_sock > 0) {
    sys_close(data->rtcp_sock);
  }
  xfree(data);

  return 0;
}

int libmedia_node_rtp(int pe) {
  rtp_node_t *data;
  script_int_t my_port, node;
  char *my_host = NULL;
  int port, r = -1;

  if (script_get_string(pe,  0, &my_host) == 0 &&
      script_get_integer(pe, 1, &my_port) == 0) {

    if ((data = xcalloc(1, sizeof(rtp_node_t))) != NULL) {
      port = my_port;
      if ((data->rtp_sock = sys_socket_bind(my_host, &port, IP_DGRAM)) != -1) {
        port++;
        data->rtcp_sock = sys_socket_bind(my_host, &port, IP_DGRAM);
        data->first = 1;
        data->clockrate = 90000;
        data->ssrc = sys_rand() ^ sys_rand();
        if ((node = node_create("RTP", &rtp_dispatch, data)) != -1) {
          r = script_push_integer(pe, node);
        }
      }
    }
  }

  if (my_host) xfree(my_host);

  return r;
}
