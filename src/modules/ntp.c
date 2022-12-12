/*
 * ntp.c
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#include <quectel-ec25/ntp.h>
#if QTEL_EN_FEATURE_NTP

#include "../include/quectel-ec25.h"
#include <quectel-ec25/core.h>
#include <quectel-ec25/net.h>
#include <quectel-ec25/utils.h>
#include "../events.h"
#include <stdlib.h>
#include <string.h>

static uint8_t syncNTP(QTEL_NTP_HandlerTypeDef*);
static void onSynced(void *app, AT_Data_t*);


QTEL_Status_t QTEL_NTP_Init(QTEL_NTP_HandlerTypeDef *hsimntp, void *hsim)
{
  if (((QTEL_HandlerTypeDef*)hsim)->key != QTEL_KEY)
    return QTEL_ERROR;

  hsimntp->hsim = hsim;
  hsimntp->status = 0;
  hsimntp->config.resyncInterval = 24*3600;
  hsimntp->config.retryInterval = 5000;

  AT_On(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+QNTP", (QTEL_HandlerTypeDef*) hsim, 0, 0, onSynced);

  return QTEL_OK;
}


QTEL_Status_t QTEL_NTP_SetupServer(QTEL_NTP_HandlerTypeDef *hsimntp,
                                 char *server, uint16_t port)
{
  hsimntp->server = server;
  if (port == 0)
    hsimntp->port = 123;
  else
    hsimntp->port = port;

  QTEL_UNSET_STATUS(hsimntp, QTEL_NTP_SERVER_WAS_SET);
  QTEL_UNSET_STATUS(hsimntp, QTEL_NTP_WAS_SYNCED);
  return QTEL_OK;
}


QTEL_Status_t QTEL_NTP_Loop(QTEL_NTP_HandlerTypeDef *hsimntp)
{
  QTEL_HandlerTypeDef *hsim = hsimntp->hsim;

  if (hsim->net.state != QTEL_NET_STATE_ONLINE) return QTEL_ERROR;
  if (!QTEL_IS_STATUS(hsimntp, QTEL_NTP_SERVER_WAS_SET)) {
    QTEL_NTP_SetServer(&hsim->ntp);
  } else {
    if (QTEL_IS_STATUS(hsimntp, QTEL_NTP_WAS_SYNCED)) {
      if (hsim->getTick() - hsimntp->syncTick > hsimntp->config.resyncInterval)
        syncNTP(hsimntp);
    }
    else {
      if (hsim->getTick() - hsimntp->syncTick > hsimntp->config.retryInterval)
        syncNTP(hsimntp);
    }
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_NTP_SetServer(QTEL_NTP_HandlerTypeDef *hsimntp)
{
  if (hsimntp->server == 0 || strlen(hsimntp->server) == 0) return QTEL_ERROR;

  QTEL_HandlerTypeDef *hsim = hsimntp->hsim;
  AT_Data_t paramData[3] = {
    AT_Number(1),               // cid
    AT_String(hsimntp->server),
    AT_Number(hsimntp->port),
  };

  if (AT_Command(&hsim->atCmd, "+QNTP", 3, paramData, 0, 0) != AT_OK) return QTEL_ERROR;
  QTEL_SET_STATUS(hsimntp, QTEL_NTP_SERVER_WAS_SET);
  syncNTP(hsimntp);
  return QTEL_OK;
}


QTEL_Status_t QTEL_NTP_OnSynced(QTEL_NTP_HandlerTypeDef *hsimntp)
{
  QTEL_HandlerTypeDef *hsim = hsimntp->hsim;
  QTEL_Datetime_t dt;

  if (hsimntp->onSynced != 0 && QTEL_GetTime(hsim, &dt) == QTEL_OK) {
    hsimntp->onSynced(dt);
  }

  return QTEL_OK;
}


static uint8_t syncNTP(QTEL_NTP_HandlerTypeDef *hsimntp)
{
  QTEL_HandlerTypeDef *hsim = hsimntp->hsim;

  hsimntp->syncTick = hsim->getTick();
  QTEL_Debug("[NTP] syncronizing...");
  if (AT_Command(&hsim->atCmd, "+CNTP", 0, 0, 0, 0) != AT_OK) return QTEL_ERROR;

  return QTEL_OK;
}

static void onSynced(void *app, AT_Data_t *_)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;

  hsim->status = 0;
  hsim->events = 0;
  QTEL_Debug("[NTP] synced");
  QTEL_SET_STATUS(&hsim->ntp, QTEL_NTP_WAS_SYNCED);
  hsim->rtos.eventSet(QTEL_RTOS_EVT_NTP_SYNCED);
}

#endif /* QTEL_EN_FEATURE_NTP */
