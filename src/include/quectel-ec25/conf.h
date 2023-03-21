/*
 * conf.h
 *
 *  Created on: Dec 3, 2021
 *      Author: janoko
 */

#ifndef QUECTEL_EC25_CONF_H_
#define QUECTEL_EC25_CONF_H_

#define QTEL_KEY   0xAEFF5111U

#ifndef QTEL_DEBUG
#define QTEL_DEBUG 1
#endif

/* PDP Context ID */

#ifndef QTEL_CID_SOCKET
#define QTEL_CID_SOCKET 2
#endif

#ifndef QTEL_CID_HTTP
#define QTEL_CID_HTTP   3
#endif

#ifndef QTEL_CID_NTP
#define QTEL_CID_NTP    4
#endif


#ifndef QTEL_EN_FEATURE_GPS
#define QTEL_EN_FEATURE_GPS 0
#endif
#ifndef QTEL_EN_FEATURE_GPS_ONEXTRA
#define QTEL_EN_FEATURE_GPS_ONEXTRA 1
#endif

#if QTEL_EN_FEATURE_GPS & QTEL_EN_FEATURE_GPS_ONEXTRA
#ifdef QTEL_EN_FEATURE_HTTP
#undef QTEL_EN_FEATURE_HTTP
#endif
#define QTEL_EN_FEATURE_HTTP 1
#endif

#ifndef QTEL_EN_FEATURE_SOCKET
#define QTEL_EN_FEATURE_SOCKET 0
#endif

#ifndef QTEL_EN_FEATURE_NTP
#define QTEL_EN_FEATURE_NTP 0
#endif

#ifndef QTEL_EN_FEATURE_HTTP
#define QTEL_EN_FEATURE_HTTP 0
#endif

#define QTEL_EN_FEATURE_NET QTEL_EN_FEATURE_NTP|QTEL_EN_FEATURE_SOCKET|QTEL_EN_FEATURE_HTTP

#ifndef QTEL_NUM_OF_SOCKET
#define QTEL_NUM_OF_SOCKET  10
#endif

#ifndef QTEL_EN_FEATURE_FILE
#define QTEL_EN_FEATURE_FILE QTEL_EN_FEATURE_HTTP
#endif

#ifndef QTEL_DEBUG
#define QTEL_DEBUG 1
#endif

#ifndef QTEL_CMD_BUFFER_SIZE
#define QTEL_CMD_BUFFER_SIZE  256
#endif

#ifndef QTEL_RESP_BUFFER_SIZE
#define QTEL_RESP_BUFFER_SIZE  256
#endif

#ifndef QTEL_RESP_CONNECT_BUFFER_SIZE
#define QTEL_RESP_CONNECT_BUFFER_SIZE  32
#endif

#if QTEL_EN_FEATURE_NTP
#ifndef QTEL_NTP_SYNC_DELAY_TIMEOUT
#define QTEL_NTP_SYNC_DELAY_TIMEOUT 10000
#endif
#endif

#ifndef LWGPS_IGNORE_USER_OPTS
#define LWGPS_IGNORE_USER_OPTS
#endif

#if QTEL_EN_FEATURE_GPS
#ifndef QTEL_GPS_TMP_BUF_SIZE
#define QTEL_GPS_TMP_BUF_SIZE  64
#endif
#endif /* QTEL_EN_FEATURE_GPS */

#endif /* QUECTEL_EC25_CONF_H_ */
