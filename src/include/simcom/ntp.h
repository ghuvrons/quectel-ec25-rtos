/*
 * ntp.h
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#ifndef SIMCOM_7600E_NTP_H_
#define SIMCOM_7600E_NTP_H_

#include "conf.h"
#if SIM_EN_FEATURE_NTP

#include "types.h"

#define SIM_NTP_SERVER_WAS_SET 0x01
#define SIM_NTP_WAS_SYNCED     0x02

typedef struct {
  void        *hsim;
  uint8_t     status;
  char        *server;
  int8_t      region;
  uint32_t    syncTick;
  void (*onSynced)(SIM_Datetime_t);

  struct {
    uint32_t retryInterval;
    uint32_t resyncInterval;
  } config;
} SIM_NTP_HandlerTypeDef;

SIM_Status_t SIM_NTP_Init(SIM_NTP_HandlerTypeDef*, void *hsim);
SIM_Status_t SIM_NTP_SetupServer(SIM_NTP_HandlerTypeDef*, char *server, int8_t region);
SIM_Status_t SIM_NTP_Loop(SIM_NTP_HandlerTypeDef*);

SIM_Status_t SIM_NTP_SetServer(SIM_NTP_HandlerTypeDef*);
SIM_Status_t SIM_NTP_OnSynced(SIM_NTP_HandlerTypeDef*);


#endif /* SIM_EN_FEATURE_NTP */
#endif /* SIMCOM_7600E_NTP_H_ */
