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


QTEL_Status_t QTEL_NTP_Init(QTEL_NTP_HandlerTypeDef *qtelNTP, void *qtelPtr)
{
  if (((QTEL_HandlerTypeDef*)qtelPtr)->key != QTEL_KEY)
    return QTEL_ERROR;

  qtelNTP->qtel = qtelPtr;
  qtelNTP->status = 0;
  qtelNTP->config.resyncInterval = 24*3600;
  qtelNTP->config.retryInterval = 5000;

  AT_On(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "+QNTP", (QTEL_HandlerTypeDef*) qtelPtr, 0, 0, onSynced);

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

  if (qtelPtr->net.state != QTEL_NET_STATE_ONLINE) return QTEL_ERROR;

  if (QTEL_IS_STATUS(qtelNTP, QTEL_NTP_WAS_SYNCED)) {
    if (qtelPtr->getTick() - qtelNTP->syncTick > qtelNTP->config.resyncInterval)
      syncNTP(qtelNTP);
  }
  else {
    if (qtelPtr->getTick() - qtelNTP->syncTick > qtelNTP->config.retryInterval)
      syncNTP(qtelNTP);
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_NTP_OnSynced(QTEL_NTP_HandlerTypeDef *qtelNTP)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelNTP->qtel;
  QTEL_Datetime_t dt;

  if (qtelNTP->onSynced != 0 && QTEL_GetTime(qtelPtr, &dt) == QTEL_OK) {
    qtelNTP->onSynced(dt);
  }

  return QTEL_OK;
}


static uint8_t syncNTP(QTEL_NTP_HandlerTypeDef *qtelNTP)
{
  if (qtelNTP->server == 0 || strlen(qtelNTP->server) == 0) return QTEL_ERROR;

  QTEL_HandlerTypeDef *qtelPtr = qtelNTP->qtel;
  AT_Data_t paramData[3] = {
    AT_Number(1),               // cid
    AT_String(qtelNTP->server),
    AT_Number(qtelNTP->port),
  };

  QTEL_Debug("[NTP] syncronizing...");
  qtelNTP->syncTick = qtelPtr->getTick();
  if (AT_Command(&qtelPtr->atCmd, "+QNTP", 3, paramData, 0, 0) != AT_OK) return QTEL_ERROR;\

  return QTEL_OK;
}

static void onSynced(void *app, AT_Data_t *_)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;

  qtelPtr->status = 0;
  qtelPtr->events = 0;
  QTEL_Debug("[NTP] synced");
  QTEL_SET_STATUS(&qtelPtr->ntp, QTEL_NTP_WAS_SYNCED);
  qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_NTP_SYNCED);
}

#endif /* QTEL_EN_FEATURE_NTP */
