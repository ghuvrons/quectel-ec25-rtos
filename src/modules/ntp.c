/*
 * ntp.c
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#include "../include/simcom/ntp.h"
#if SIM_EN_FEATURE_NTP

#include "../include/simcom.h"
#include "../include/simcom/core.h"
#include "../include/simcom/net.h"
#include "../include/simcom/utils.h"
#include "../events.h"
#include <stdlib.h>
#include <string.h>

static uint8_t syncNTP(SIM_NTP_HandlerTypeDef*);
static void onSynced(void *app, AT_Data_t*);


SIM_Status_t SIM_NTP_Init(SIM_NTP_HandlerTypeDef *hsimntp, void *hsim)
{
  if (((SIM_HandlerTypeDef*)hsim)->key != SIM_KEY)
    return SIM_ERROR;

  hsimntp->hsim = hsim;
  hsimntp->status = 0;
  hsimntp->config.resyncInterval = 24*3600;
  hsimntp->config.retryInterval = 5000;

  AT_On(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+CNTP", (SIM_HandlerTypeDef*) hsim, 0, 0, onSynced);

  return SIM_OK;
}


SIM_Status_t SIM_NTP_SetupServer(SIM_NTP_HandlerTypeDef *hsimntp,
                                 char *server, int8_t region)
{
  hsimntp->server = server;
  hsimntp->region = region;
  SIM_UNSET_STATUS(hsimntp, SIM_NTP_SERVER_WAS_SET);
  SIM_UNSET_STATUS(hsimntp, SIM_NTP_WAS_SYNCED);
  return SIM_OK;
}


SIM_Status_t SIM_NTP_Loop(SIM_NTP_HandlerTypeDef *hsimntp)
{
  SIM_HandlerTypeDef *hsim = hsimntp->hsim;

  if (hsim->net.state != SIM_NET_STATE_ONLINE) return SIM_ERROR;
  if (!SIM_IS_STATUS(hsimntp, SIM_NTP_SERVER_WAS_SET)) {
    SIM_NTP_SetServer(&hsim->ntp);
  } else {
    if (SIM_IS_STATUS(hsimntp, SIM_NTP_WAS_SYNCED)) {
      if (hsim->getTick() - hsimntp->syncTick > hsimntp->config.resyncInterval)
        syncNTP(hsimntp);
    }
    else {
      if (hsim->getTick() - hsimntp->syncTick > hsimntp->config.retryInterval)
        syncNTP(hsimntp);
    }
  }

  return SIM_OK;
}


SIM_Status_t SIM_NTP_SetServer(SIM_NTP_HandlerTypeDef *hsimntp)
{
  if (hsimntp->server == 0 || strlen(hsimntp->server) == 0) return SIM_ERROR;

  SIM_HandlerTypeDef *hsim = hsimntp->hsim;
  AT_Data_t paramData[2] = {
    AT_String(hsimntp->server),
    AT_Number(hsimntp->region),
  };

  if (AT_Command(&hsim->atCmd, "+CNTP", 2, paramData, 0, 0) != AT_OK) return SIM_ERROR;
  SIM_SET_STATUS(hsimntp, SIM_NTP_SERVER_WAS_SET);
  syncNTP(hsimntp);
  return SIM_OK;
}


SIM_Status_t SIM_NTP_OnSynced(SIM_NTP_HandlerTypeDef *hsimntp)
{
  SIM_HandlerTypeDef *hsim = hsimntp->hsim;
  SIM_Datetime_t dt;

  if (hsimntp->onSynced != 0 && SIM_GetTime(hsim, &dt) == SIM_OK) {
    hsimntp->onSynced(dt);
  }

  return SIM_OK;
}


static uint8_t syncNTP(SIM_NTP_HandlerTypeDef *hsimntp)
{
  SIM_HandlerTypeDef *hsim = hsimntp->hsim;

  hsimntp->syncTick = hsim->getTick();
  SIM_Debug("[NTP] syncronizing...");
  if (AT_Command(&hsim->atCmd, "+CNTP", 0, 0, 0, 0) != AT_OK) return SIM_ERROR;

  return SIM_OK;
}

static void onSynced(void *app, AT_Data_t *_)
{
  SIM_HandlerTypeDef *hsim = (SIM_HandlerTypeDef*)app;

  hsim->status = 0;
  hsim->events = 0;
  SIM_Debug("[NTP] synced");
  SIM_SET_STATUS(&hsim->ntp, SIM_NTP_WAS_SYNCED);
  hsim->rtos.eventSet(SIM_RTOS_EVT_NTP_SYNCED);
}

#endif /* SIM_EN_FEATURE_NTP */
