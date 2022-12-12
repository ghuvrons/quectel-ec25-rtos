/*
 * events.h
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#ifndef QUECTEL_EC25_EVENTS_H
#define QUECTEL_EC25_EVENTS_H

#define QTEL_RTOS_EVT_READY              0x0010U
#define QTEL_RTOS_EVT_NEW_STATE          0x0020U
#define QTEL_RTOS_EVT_ACTIVED            0x0040U
//socket
#define QTEL_RTOS_EVT_GPS_NEW_STATE      0x0080U
// net
#define QTEL_RTOS_EVT_NET_NEW_STATE      0x0100U
//socket
#define QTEL_RTOS_EVT_SOCKMGR_NEW_STATE  0x0200U
#define QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT 0x0400U
// ntp
#define QTEL_RTOS_EVT_NTP_SYNCED         0x0800U
// ntp
#define QTEL_RTOS_EVT_HTTP_NEW_STATE     0x1000U

#define QTEL_RTOS_EVT_ALL  QTEL_RTOS_EVT_READY | QTEL_RTOS_EVT_NEW_STATE | QTEL_RTOS_EVT_ACTIVED |\
                          QTEL_RTOS_EVT_GPS_NEW_STATE | QTEL_RTOS_EVT_NET_NEW_STATE |\
                          QTEL_RTOS_EVT_SOCKMGR_NEW_STATE | QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT |\
                          QTEL_RTOS_EVT_NTP_SYNCED



#endif /* QUECTEL_EC25_EVENTS_H */
