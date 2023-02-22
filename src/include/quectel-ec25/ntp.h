/*

 * ntp.h
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#ifndef QUECTEL_EC25_NTP_H_
#define QUECTEL_EC25_NTP_H_

#include <quectel-ec25/conf.h>
#if QTEL_EN_FEATURE_NTP

#include <quectel-ec25/types.h>

#define QTEL_NTP_WAS_SYNCING    0x01
#define QTEL_NTP_WAS_SYNCED     0x02

typedef struct {
  void      *qtel;
  uint8_t   status;
  uint8_t   contextId;
  char      *server;
  uint16_t  port;
  uint32_t  syncTick;
  void      (*onSynced)(QTEL_Datetime_t);

  struct {
    uint32_t retryInterval;
    uint32_t resyncInterval;
  } config;
} QTEL_NTP_HandlerTypeDef;

QTEL_Status_t QTEL_NTP_Init(QTEL_NTP_HandlerTypeDef*, void *qtelPtr);
QTEL_Status_t QTEL_NTP_SetupServer(QTEL_NTP_HandlerTypeDef*, char *server, uint16_t port);
QTEL_Status_t QTEL_NTP_Loop(QTEL_NTP_HandlerTypeDef*);
QTEL_Status_t QTEL_NTP_Sync(QTEL_NTP_HandlerTypeDef*);
QTEL_Status_t QTEL_NTP_OnSyncingFinish(QTEL_NTP_HandlerTypeDef*);


#endif /* QTEL_EN_FEATURE_NTP */
#endif /* QUECTEL_EC25_NTP_H_ */
