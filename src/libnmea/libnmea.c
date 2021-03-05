#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "script.h"
#include "io.h"
#include "sock.h"
#include "gps.h"
#include "thread.h"
#include "ts.h"
#include "sys.h"
#include "meters.h"
#include "timeutc.h"
#include "debug.h"
#include "xalloc.h"

#define KNOT_TO_MS        (1852.0/3600.0)
#define MAX_NMEA_ARGS     64
#define MAX_MTK_SENTENCES 19

#define TAG_NMEA  "NMEA"

static unsigned char hex_digit(char c) {
  unsigned char digit = 0;

  if (c >= '0' && c <= '9') digit = c - '0';
  else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
  else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;

  return digit;
}

static int nmea_gpgga(char *arg[], int n, gps_t *gps) {
  //        0      1        2 3         4 5 6  7   8     9 0    1
  // $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
  // $GPGGA,201745.000,1955.5153,S,04356.3875,W,1,05,2.4,953.4,M,-5.7,M,,0000*46
  // $GPGGA,125722.000,,,,,0,00,,,M,0.0,M,,0000*57

  if (n >= 9 && strlen(arg[8]) >= 2) {
    gps->height = atof(arg[8]);
    if (arg[9][0] == 'f' || arg[9][0] == 'F') {
      gps->height = f2m(gps->height);
    }
    return 0;
  }

  gps->height = 0;
  return -1;
}

static int nmea_gpgsa(char *arg[], int n, gps_t *gps) {
  // $GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39
  // $GPGSA,A,3,20,32,01,23,31,,,,,,,,5.1,2.4,4.5*30

  int id, i;

  if (n >= 2) {
    if (n >= 14) {
      for (i = 0; i < MAX_SATS; i++) {
        gps->used[i] = 0;
      }
      gps->nused = 0;
      for (i = 2; i < 14; i++) {
        if (arg[i][0]) {
          id = atoi(arg[i]);
          gps->used[gps->nused++] = id;
          debug(DEBUG_TRACE, "NMEA", "satellite prn %d used", id);
        }
      }
    }
    gps->mode = atoi(arg[1]);
    return 0;
  }

  gps->mode = 1;
  return -1;
}

static int nmea_gprmc(char *arg[], int n, gps_t *gps) {
  // $GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
  // $GPRMC,201745.000,A,1955.5153,S,04356.3875,W,0.00,,220514,,,A*7D
  // $GPRMC,121454.000,V,,,,,,,170515,,,N*4D

  struct tm tm;
  time_t t;
  char buf[8];

  if (n > 0 && strlen(arg[0]) >= 6) {
    gps->hour = 10*(arg[0][0]-'0') + arg[0][1]-'0';
    gps->min = 10*(arg[0][2]-'0') + arg[0][3]-'0';
    gps->sec = 10*(arg[0][4]-'0') + arg[0][5]-'0';
  } else {
    gps->hour = 0;
    gps->min = 0;
    gps->sec = 0;
  }

  if (n > 8 && strlen(arg[8]) == 6) {
    gps->day = 10*(arg[8][0]-'0') + arg[8][1]-'0';
    gps->month = 10*(arg[8][2]-'0') + arg[8][3]-'0';
    gps->year = 10*(arg[8][4]-'0') + arg[8][5]-'0';
    gps->year += gps->year < 80 ? 2000 : 1900;
  } else {
    gps->day = 0;
    gps->month = 0;
    gps->year = 0;
  }

  if (n > 3 && strlen(arg[2]) >= 3 && arg[1][0] == 'A') {
    buf[0] = arg[2][0];
    buf[1] = arg[2][1];
    buf[2] = 0;
    gps->lat = atof(buf);
    gps->lat += atof(&arg[2][2]) / 60.0;
    if (arg[3][0] == 'S') {
      gps->lat = -gps->lat;
    }
  } else {
    gps->lat = 0;
  }

  if (n > 5 && strlen(arg[4]) >= 4 && arg[1][0] == 'A') {
    buf[0] = arg[4][0];
    buf[1] = arg[4][1];
    buf[2] = arg[4][2];
    buf[3] = 0;
    gps->lon = atof(buf);
    gps->lon += atof(&arg[4][3]) / 60.0;
    if (arg[5][0] == 'W') {
      gps->lon = -gps->lon;
    }
  } else {
    gps->lon = 0;
  }

  if (n > 6 && arg[1][0] == 'A') {
    gps->speed = arg[6][0] ? atof(arg[6]) * KNOT_TO_MS : 0;
  } else {
    gps->speed = 0;
  }

  if (n > 7 && arg[1][0] == 'A') {
    gps->course = arg[7][0] ? atof(arg[7]) : 0;
  } else {
    gps->course = 0;
  }

  if (gps->year > 0 && gps->month > 0 && gps->day > 0) {
    debug(DEBUG_TRACE, "NMEA", "GPRMC %04d-%02d-%02d %02d:%02d:%02d %.8f %.8f %.1f %.1f %.1f %d", gps->year, gps->month, gps->day, gps->hour, gps->min, gps->sec, gps->lon, gps->lat, gps->height, gps->speed, gps->course, gps->mode);
    gps->ts = time2ts(gps->day, gps->month, gps->year, gps->hour, gps->min, gps->sec);
  }

  if (gps->mode >= 2) {
    gps->newpos = 1;

    t = sys_time();
    utctime(&t, &tm);
    if (((tm.tm_year + 1900) - gps->year) >= 15) {
      debug(DEBUG_INFO, "NMEA", "fixing GPS week rollover");
      gps->ts += 1024L*7*24*60*60;
      t = gps->ts;
      utctime(&t, &tm);
      gps->year = tm.tm_year + 1900;
      gps->month = tm.tm_mon + 1;
      gps->day = tm.tm_mday;
      debug(DEBUG_INFO, "NMEA", "fixed GPRMC %04d-%02d-%02d %02d:%02d:%02d %.8f %.8f %.1f %.1f %.1f %d", gps->year, gps->month, gps->day, gps->hour, gps->min, gps->sec, gps->lon, gps->lat, gps->height, gps->speed, gps->course, gps->mode);
    }
  }

  return 0;
}

static int nmea_gpgsv(char *arg[], int n, gps_t *gps) {
  // $GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74
  // $GPGSV,3,2,11,14,25,170,00,16,57,208,39,18,67,296,40,19,40,246,00*74
  // $GPGSV,3,3,11,22,42,067,42,24,14,311,43,27,05,244,00,,,,*4D

  int part, i, j, k;

  if (n >= 3) {
    part = atoi(arg[1]);
    if (part >= 1) {
      part--;
      gps->newsat = 1;
      gps->nsats = atoi(arg[2]);
      if (gps->nsats > MAX_SATS) gps->nsats = MAX_SATS;
      i = part * 4;
      if (i == 0) {
        for (j = 0; j < MAX_SATS; j++) {
          gps->sat[j].prn = 0;
          gps->sat[j].elevation = 0;
          gps->sat[j].azimuth = 0;
          gps->sat[j].snr = 0;
          gps->sat[j].used = 0;
        }
      }
      for (j = 0; j < 4; j++) {
        if (i+j < gps->nsats && n >= 3 + 4 * (j + 1)) {
          gps->sat[i+j].prn = atoi(arg[3 + 4 * j]);
          gps->sat[i+j].elevation = atoi(arg[3 + 4 * j + 1]);
          gps->sat[i+j].azimuth = atoi(arg[3 + 4 * j + 2]);
          gps->sat[i+j].snr = atoi(arg[3 + 4 * j + 3]);
          for (k = 0; k < gps->nused; k++) {
            if (gps->sat[i+j].prn == gps->used[k]) {
              gps->sat[i+j].used = 1;
            }
          }
          debug(DEBUG_TRACE, "NMEA", "satellite %d/%d: prn=%d elev=%d azi=%d snr=%d", i+j+1, gps->nsats,
            gps->sat[i+j].prn, gps->sat[i+j].elevation, gps->sat[i+j].azimuth, gps->sat[i+j].snr);
        }
      }
    }
  }

  return 0;
}

static int parse_nmea(char *buf, gps_t *gps, int fd) {
  int i, k, r = -1;
  char *cmd;
  unsigned char sum1, sum2;
  char *arg[MAX_NMEA_ARGS];

  if (buf[0] == '$') {
    debug(gps->mode >= 2 ? DEBUG_TRACE : DEBUG_INFO, "NMEA", "sentence [%s]", buf);
    cmd = &buf[1];
    sum1 = 0;

    for (i = 1, k = 0; buf[i] && k < MAX_NMEA_ARGS; i++) {
      if (buf[i] == '*') {
        sum2 = hex_digit(buf[i+1])*16 + hex_digit(buf[i+2]);
        if (sum1 != sum2) {
          debug(DEBUG_ERROR, "NMEA", "invalid NMEA checksum 0x%02X 0x%02X", sum1, sum2);
          return -1;
        }
        break;
      }
      sum1 ^= buf[i];
      if (buf[i] == ',') {
        buf[i] = 0;
        arg[k++] = &buf[i+1];
      }
    }

    if (k > 0) {
      if (!strcmp(cmd, "GPGSA")) {
        r = nmea_gpgsa(arg, k, gps);
      } else if (!strcmp(cmd, "GPGGA")) {
        r = nmea_gpgga(arg, k, gps);
      } else if (!strcmp(cmd, "GPRMC")) {
        r = nmea_gprmc(arg, k, gps);
      } else if (!strcmp(cmd, "GPGSV")) {
        r = nmea_gpgsv(arg, k, gps);
      } else {
        debug(DEBUG_TRACE, "NMEA", "ignoring NMEA sentence %s", cmd);
      }
    }
  } else if (buf[0]) {
    debug(DEBUG_ERROR, "NMEA", "invalid NMEA [%s]", buf);
  }

  return r;
}

static void nmea_checksum(char *buf, int buflen) {
  unsigned char sum;
  char *p;
  int len;

  for (p = buf+1, sum = 0; *p && *p != '*'; p++) sum ^= *p;
  len = strlen(buf);
  snprintf(buf+len, buflen-1, "%02x\r\n", sum);
}

/*
  SiRF proprietary NMEA $PSRF103 message
  type: 0=GGA, 1=GLL, 2=GSA, 3=GSV, 4=RMC, 5=VTG, 6=MSS, 7=undefined, 8=ZDA, 9=undefined
  mode: 0=set, 1=get
  rate: 0=off, 255=max, unit is seconds

  $PSRF103,00,00,05,01*21<0D><0A>
  $PSRF103,01,00,00,01*25<0D><0A>
  $PSRF103,02,00,05,01*23<0D><0A>
  $PSRF103,03,00,00,01*27<0D><0A>
  $PSRF103,04,00,05,01*25<0D><0A>
  $PSRF103,05,00,00,01*21<0D><0A>
*/

static int sirf_setrate(int handle, int type, int rate) {
  char buf[256];
  int len, mode = 0, usecksum = 1;

  debug(DEBUG_INFO, "NMEA", "setting SIRF rate for message %d to %d second(s)", type, rate);
  memset(buf, 0, sizeof(buf));

  snprintf(buf, sizeof(buf)-1, "$PSRF103,%02d,%02d,%02d,%02d*", type, mode, rate, usecksum);
  nmea_checksum(buf, sizeof(buf));
  debug(DEBUG_INFO, "NMEA", "%s", buf);
  len = strlen(buf);

  return io_write_handle(TAG_NMEA, handle, (unsigned char *)buf, len);
}

static int libnmea_sirf_rate(int pe) {
  script_int_t handle, type, rate;
  int r = -1;

  if (script_get_integer(pe, 0, &handle) == 0 &&
      script_get_integer(pe, 1, &type) == 0 &&
      script_get_integer(pe, 2, &rate) == 0) {

    if (type >= 0 && type <= 9 && rate >= 0 && rate <= 255) {
      r = script_push_boolean(pe, sirf_setrate(handle, type, rate) == 0);
    } else {
      debug(DEBUG_ERROR, "NMEA", "invalid set rate parameters");
    }
  }

  return r;
}

/*
   Index:
   0 GPGLL:   Geographic Position
   1 GPRMC:   Recommended Minimum Specific GNSS Sentence
   2 GPVTG:   Course Over Ground and Ground Speed
   3 GPGGA:   GPS Fix Data
   4 GPGSA:   GNSS DOPS and Active Satellites
   5 GPGSV:   GNSS Satellites in View
   6 GPGRS:   GNSS Range Residuals
   7 GPGST:   GNSS Pseudorange Errors Statistics
  13 PMTKALM: GPS almanac information
  14 PMTKEPH: GPS ephemeris information
  15 PMTKDGP: GPS differential correction information
  16 PMTKDBG: MTK debug information
  17 GPZDA:   Time & Date
  18 PMTKCHN: GPS channel status

  Value n: output sentence every n position fixes (0 <= n <=5)
*/

static int mtk_setrate(int handle, int *rate) {
  char buf[256];
  int len;

  debug(DEBUG_INFO, "NMEA", "setting MTK rates");
  memset(buf, 0, sizeof(buf));

  snprintf(buf, sizeof(buf)-1, "$PMTK314,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d*",
    rate[0], rate[1], rate[2], rate[3], rate[4], rate[5], rate[6], rate[7], rate[8], rate[9],
    rate[10], rate[11], rate[12], rate[13], rate[14], rate[15], rate[16], rate[17], rate[18]);
  nmea_checksum(buf, sizeof(buf));
  debug(DEBUG_INFO, "NMEA", "%s", buf);
  len = strlen(buf);

  return io_write_handle(TAG_NMEA, handle, (unsigned char *)buf, len);
}

static int libnmea_mtk_rate(int pe) {
  int rate[MAX_MTK_SENTENCES];
  script_int_t handle, aux;
  int i, r = -1;

  if (script_get_integer(pe, 0, &handle) == 0) {
    for (i = 0; i < MAX_MTK_SENTENCES; i++) {
      if (script_get_integer(pe, i+1, &aux) != 0) break;
      rate[i] = aux;
    }
    for (; i < MAX_MTK_SENTENCES; i++) {
      rate[i] = 0;
    }

    r = script_push_boolean(pe, mtk_setrate(handle, rate) == 0);
  }

  return r;
}

static int ubx_setrate(int handle, int type, int rate) {
  char buf[256], *sentence;
  int len;

  switch (type) {
    case 0: sentence = "GGA"; break;
    case 1: sentence = "GLL"; break;
    case 2: sentence = "GSA"; break;
    case 3: sentence = "GSV"; break;
    case 4: sentence = "RMC"; break;
    case 5: sentence = "VTG"; break;
    default:
      debug(DEBUG_ERROR, "NMEA", "invalid UBX set rate message %d", type);
      return -1;
  }

  debug(DEBUG_INFO, "NMEA", "setting UBX rate for message %s to %d cycles(s)", sentence, rate);
  memset(buf, 0, sizeof(buf));

  // $PUBX,40,msgId,rddc,rus1,rus2,rusb,rspi,reserved

  snprintf(buf, sizeof(buf)-1, "$PUBX,40,%s,%d,%d,%d,%d,%d,0*", sentence, rate, rate, rate, rate, rate);
  nmea_checksum(buf, sizeof(buf));
  debug(DEBUG_INFO, "NMEA", "%s", buf);
  len = strlen(buf);

  return io_write_handle(TAG_NMEA, handle, (unsigned char *)buf, len);
}

static int libnmea_ubx_rate(int pe) {
  script_int_t handle, type, rate;
  int r = -1;

  if (script_get_integer(pe, 0, &handle) == 0 &&
      script_get_integer(pe, 1, &type) == 0 &&
      script_get_integer(pe, 2, &rate) == 0) {

    if (type >= 0 && type <= 5 && rate >= 0 && rate <= 255) {
      r = script_push_boolean(pe, ubx_setrate(handle, type, rate) == 0);
    } else {
      debug(DEBUG_ERROR, "NMEA", "invalid set rate parameters");
    }
  }

  return r;
}

static int garmin_enable(int handle, int type, int enable) {
  char buf[256], *sentence;
  int len;

  switch (type) {
    case 0: sentence = "GPGGA"; break;
    case 1: sentence = "GPGLL"; break;
    case 2: sentence = "GPGSA"; break;
    case 3: sentence = "GPGSV"; break;
    case 4: sentence = "GPRMC"; break;
    case 5: sentence = "GPVTG"; break;
    default:
      debug(DEBUG_ERROR, "NMEA", "invalid Garmin enable message %d", type);
      return -1;
  }

  debug(DEBUG_INFO, "NMEA", "setting Garmin enable message %s to %d", sentence, enable);
  memset(buf, 0, sizeof(buf));

  snprintf(buf, sizeof(buf)-1, "$PGRMO,%s,%d*", sentence, enable);
  nmea_checksum(buf, sizeof(buf));
  debug(DEBUG_INFO, "NMEA", "%s", buf);
  len = strlen(buf);

  return io_write_handle(TAG_NMEA, handle, (unsigned char *)buf, len);
}

static int libnmea_garmin_enable(int pe) {
  script_int_t handle, type, enable;
  int r = -1;

  if (script_get_integer(pe, 0, &handle) == 0 &&
      script_get_integer(pe, 1, &type) == 0 &&
      script_get_integer(pe, 2, &enable) == 0) {

    if (type >= 0 && type <= 5 && enable >= 0 && enable <= 1) {
      r = script_push_boolean(pe, garmin_enable(handle, type, enable) == 0);
    } else {
      debug(DEBUG_ERROR, "NMEA", "invalid enable parameters");
    }
  }

  return r;
}

static gps_t *libnmea_getdata(int pe, script_ref_t ref) {
  gps_t *gps;

  if ((gps = xcalloc(1, sizeof(gps_t))) != NULL) {
    gps->tag = TAG_NMEA;
    gps->parse_data = NULL;
    gps->parse_line = parse_nmea;
    gps->cmd = NULL;
    gps->pe = pe;
    gps->ref = ref;
  }

  return gps;
}

static int libnmea_open(int pe) {
  script_ref_t ref;
  gps_t *gps;
  int r = -1;

  if (script_get_function(pe, 0, &ref) == 0) {
    if ((gps = libnmea_getdata(pe, ref)) != NULL) {
      r = gps_client(gps);
    }
  }

  return r;
}

static int libnmea_close(int pe) {
  return sock_stream_close(TAG_NMEA, pe);
}

int libnmea_init(int pe, script_ref_t obj) {
  debug(DEBUG_INFO, "NMEA", "registering %s", GPS_PARSE_LINE_PROVIDER);
  script_set_pointer(pe, GPS_PARSE_LINE_PROVIDER, parse_nmea);

  script_add_function(pe, obj, "open",          libnmea_open);
  script_add_function(pe, obj, "close",         libnmea_close);
  script_add_function(pe, obj, "sirf_rate",     libnmea_sirf_rate);
  script_add_function(pe, obj, "mtk_rate",      libnmea_mtk_rate);
  script_add_function(pe, obj, "ubx_rate",      libnmea_ubx_rate);
  script_add_function(pe, obj, "garmin_enable", libnmea_garmin_enable);

  return 0;
}
