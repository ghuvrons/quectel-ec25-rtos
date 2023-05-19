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

static void onSynced(void *app, AT_Data_t*);


QTEL_Status_t QTEL_NTP_Init(QTEL_NTP_HandlerTypeDef *qtelNTP, void *qtelPtr)
{
  if (((QTEL_HandlerTypeDef*)qtelPtr)->key != QTEL_KEY)
    return QTEL_ERROR;

  qtelNTP->qtel = qtelPtr;
  qtelNTP->contextId = 3;
  qtelNTP->status = 0;
  qtelNTP->config.resyncInterval = 24*3600;
  qtelNTP->config.retryInterval = 5000;

  AT_Data_t *ntpResp = malloc(sizeof(AT_Data_t));
  memset(ntpResp, 0, sizeof(AT_Data_t));
  AT_On(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "+QNTP", (QTEL_HandlerTypeDef*) qtelPtr, 1, ntpResp, onSynced);

  return QTEL_OK;
}


QTEL_Status_t QTEL_NTP_SetupServer(QTEL_NTP_HandlerTypeDef *qtelNTP, char *server, uint16_t port)
{
  qtelNTP->server = server;
  if (port == 0)
    qtelNTP->port = 123;
  else
    qtelNTP->port = port;
    
  QTEL_UNSET_STATUS(qtelNTP, QTEL_NTP_WAS_SYNCED);
  return QTEL_OK;
}


QTEL_Status_t QTEL_NTP_Loop(QTEL_NTP_HandlerTypeDef *qtelNTP)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelNTP->qtel;

  if (qtelPtr->state != QTEL_STATE_ACTIVE) return QTEL_ERROR;

  if (QTEL_IS_STATUS(qtelNTP, QTEL_NTP_WAS_SYNCED)) {
    if ((qtelPtr->getTick() - qtelNTP->syncTick) > qtelNTP->config.resyncInterval)
      QTEL_NTP_Sync(qtelNTP);
  }
  else if (!QTEL_IS_STATUS(qtelNTP, QTEL_NTP_WAS_SYNCING)) {
    if ((qtelPtr->getTick() - qtelNTP->syncTick) > qtelNTP->config.retryInterval)
      QTEL_NTP_Sync(qtelNTP);
  }
  else {
    if ((qtelPtr->getTick() - qtelNTP->syncTick) > 60000)
      QTEL_NTP_Sync(qtelNTP);
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_NTP_Sync(QTEL_NTP_HandlerTypeDef *qtelNTP)
{
  if (qtelNTP->server == 0 || strlen(qtelNTP->server) == 0) return QTEL_ERROR;

  QTEL_HandlerTypeDef *qtelPtr = qtelNTP->qtel;
  AT_Data_t paramData[3] = {
    AT_Number(qtelNTP->contextId),               // cid
    AT_String(qtelNTP->server),
    AT_Number(qtelNTP->port),
  };

  // set timezone to GMT
  AT_DataSetString(&paramData[0], "23/01/01,00:00:00+00");
  AT_Command(&qtelPtr->atCmd, "+CCLK", 1, paramData, 0, 0);

  if (QTEL_NET_ConfigureContext(&qtelPtr->net, qtelNTP->contextId) != QTEL_OK)
  {
    return QTEL_ERROR;
  }
  if (QTEL_NET_ActivateContext(&qtelPtr->net, qtelNTP->contextId) != QTEL_OK)
  {
    return QTEL_ERROR;
  }

  QTEL_Debug("[NTP] syncronizing...");
  AT_DataSetNumber(&paramData[0], qtelNTP->contextId);
  qtelNTP->syncTick = qtelPtr->getTick();
  QTEL_SET_STATUS(&qtelPtr->ntp, QTEL_NTP_WAS_SYNCING);
  if (AT_Command(&qtelPtr->atCmd, "+QNTP", 3, paramData, 0, 0) != AT_OK) {
    QTEL_UNSET_STATUS(&qtelPtr->ntp, QTEL_NTP_WAS_SYNCING);
    return QTEL_ERROR;
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_NTP_OnSyncingFinish(QTEL_NTP_HandlerTypeDef *qtelNTP)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelNTP->qtel;
  QTEL_Datetime_t dt;

  QTEL_UNSET_STATUS(&qtelPtr->ntp, QTEL_NTP_WAS_SYNCING);
  if (QTEL_NET_DeactivateContext(&qtelPtr->net, qtelNTP->contextId) != QTEL_OK)
  {
    return QTEL_ERROR;
  }

  if (QTEL_IS_STATUS(&qtelPtr->ntp, QTEL_NTP_WAS_SYNCED)) {
    if (qtelNTP->onSynced != 0 && QTEL_GetTime(qtelPtr, &dt) == QTEL_OK) {
      qtelNTP->onSynced(dt);
    }
  }

  return QTEL_OK;
}


static void onSynced(void *app, AT_Data_t *data)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;

  if (data->type == AT_NUMBER && data->value.number == 0) {
    QTEL_Debug("[NTP] synced");
    QTEL_SET_STATUS(&qtelPtr->ntp, QTEL_NTP_WAS_SYNCED);
  }
  qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_NTP_SYNCED);
}

#endif /* QTEL_EN_FEATURE_NTP */
