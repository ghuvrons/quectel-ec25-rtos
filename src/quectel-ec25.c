/*
 * simcom.c
 *
 *  Created on: Nov 7, 2022
 *      Author: janoko
 */

#include <quectel-ec25.h>
#include <quectel-ec25/core.h>
#include <quectel-ec25/utils.h>
#include "events.h"
#include <stdlib.h>

#define IS_EVENT(evt_notif, evt_wait) QTEL_BITS_IS(evt_notif, evt_wait)

static void onNewState(QTEL_HandlerTypeDef*);
static void loop(QTEL_HandlerTypeDef*);
static void onReady(void *app, AT_Data_t*);
static void onGetRespConnect(void *app, uint8_t *data, uint16_t len);

QTEL_Status_t QTEL_Init(QTEL_HandlerTypeDef *hsim)
{
  if (hsim->getTick == 0) return QTEL_ERROR;
  if (hsim->delay == 0) return QTEL_ERROR;

  hsim->atCmd.serial.readline = hsim->serial.readline;
  hsim->network_status = 0;
  hsim->isRespConnectHandle = 0;

  hsim->atCmd.serial.read     = hsim->serial.read;
  hsim->atCmd.serial.readinto = hsim->serial.readinto;
  hsim->atCmd.serial.readline = hsim->serial.readline;
  hsim->atCmd.serial.write    = hsim->serial.write;

  hsim->atCmd.rtos.mutexLock    = hsim->rtos.mutexLock;
  hsim->atCmd.rtos.mutexUnlock  = hsim->rtos.mutexUnlock;
  hsim->atCmd.rtos.eventSet     = hsim->rtos.eventSet;
  hsim->atCmd.rtos.eventWait    = hsim->rtos.eventWait;
  hsim->atCmd.rtos.eventClear   = hsim->rtos.eventClear;

  AT_Config_t config = {
      .commandTimeout = 30000,
      .checkTimeout = 500,
  };

  if (AT_Init(&hsim->atCmd, &config) != AT_OK) return QTEL_ERROR;

  AT_On(&hsim->atCmd, "RDY", hsim, 0, 0, onReady);
  AT_ReadlineOn(&hsim->atCmd, "CONNECT", hsim, onGetRespConnect);

  hsim->key = QTEL_KEY;

#if QTEL_EN_FEATURE_NET
  QTEL_NET_Init(&hsim->net, hsim);
#endif /* QTEL_EN_FEATURE_NET */

#if QTEL_EN_FEATURE_NTP
  QTEL_NTP_Init(&hsim->ntp, hsim);
#endif /* QTEL_EN_FEATURE_NTP */

#if QTEL_EN_FEATURE_SOCKET
  QTEL_SockManager_Init(&hsim->socketManager, hsim);
#endif /* QTEL_EN_FEATURE_SOCKET */


#if QTEL_EN_FEATURE_HTTP
  QTEL_HTTP_Init(&hsim->http, hsim);
#endif /* QTEL_EN_FEATURE_GPS */

#if QTEL_EN_FEATURE_GPS
  QTEL_GPS_Init(&hsim->gps, hsim);
#endif /* QTEL_EN_FEATURE_GPS */

#if QTEL_EN_FEATURE_FILE
  QTEL_FILE_Init(&hsim->file, hsim);
#endif /* QTEL_EN_FEATURE_GPS */

  hsim->tick.init = hsim->getTick();

  return QTEL_OK;
}

// SIMCOM Application Threads
void QTEL_Thread_Run(QTEL_HandlerTypeDef *hsim)
{
  uint32_t notifEvent;
  uint32_t timeout = 2000; // ms
  uint32_t lastTO = 0;

  for (;;) {
    if (hsim->rtos.eventWait(QTEL_RTOS_EVT_ALL, &notifEvent, timeout) == AT_OK) {
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_READY)) {

      }
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_NEW_STATE)) {
        onNewState(hsim);
      }
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_ACTIVED)) {
#if QTEL_EN_FEATURE_NET
        QTEL_NET_SetState(&hsim->net, QTEL_NET_STATE_CHECK_GPRS);
#endif /* QTEL_EN_FEATURE_NET */

#if QTEL_EN_FEATURE_GPS
        QTEL_GPS_SetState(&hsim->gps, QTEL_GPS_STATE_SETUP);
#endif /* QTEL_EN_FEATURE_GPS */
      }

#if QTEL_EN_FEATURE_NET
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_NET_NEW_STATE)) {
        QTEL_NET_OnNewState(&hsim->net);
      }
#endif /* QTEL_EN_FEATURE_NET */

#if QTEL_EN_FEATURE_SOCKET
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_SOCKMGR_NEW_STATE)) {
        QTEL_SockManager_OnNewState(&hsim->socketManager);
      }
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT)) {
        QTEL_SockManager_CheckSocketsEvents(&hsim->socketManager);
      }
#endif /* QTEL_EN_FEATURE_SOCKET */

#if QTEL_EN_FEATURE_NTP
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_NTP_SYNCED)) {
        QTEL_NTP_OnSynced(&hsim->ntp);
      }
#endif /* QTEL_EN_FEATURE_NTP */

#if QTEL_EN_FEATURE_GPS
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_GPS_NEW_STATE)) {
        QTEL_GPS_OnNewState(&hsim->gps);
      }
#endif /* QTEL_EN_FEATURE_GPS */

      goto next;
    }

    lastTO = hsim->getTick();
    loop(hsim);

#if QTEL_EN_FEATURE_NET
    QTEL_NET_Loop(&hsim->net);
#endif /* QTEL_EN_FEATURE_NET */

#if QTEL_EN_FEATURE_SOCKET
    QTEL_SockManager_Loop(&hsim->socketManager);
#endif /* QTEL_EN_FEATURE_SOCKET */

#if QTEL_EN_FEATURE_NTP
    QTEL_NTP_Loop(&hsim->ntp);
#endif /* QTEL_EN_FEATURE_NTP */

  next:
    timeout = 1000 - (hsim->getTick() - lastTO);
    if (timeout > 1000) timeout = 1;
  }
}


// AT Command Threads
void QTEL_Thread_ATCHandler(QTEL_HandlerTypeDef *hsim)
{
  AT_Process(&hsim->atCmd);
}

void QTEL_SetState(QTEL_HandlerTypeDef *hsim, uint8_t newState)
{
  hsim->state = newState;
  hsim->rtos.eventSet(QTEL_RTOS_EVT_NEW_STATE);
}

static void onNewState(QTEL_HandlerTypeDef *hsim)
{
  hsim->tick.changedState = hsim->getTick();

  switch (hsim->state) {
  case QTEL_STATE_NON_ACTIVE:
  case QTEL_STATE_CHECK_AT:
    if (QTEL_CheckAT(hsim) == QTEL_OK) {
      QTEL_SetState(hsim, QTEL_STATE_CHECK_SIMCARD);
    }
    break;

  case QTEL_STATE_CHECK_SIMCARD:
    QTEL_Debug("Checking SIM Card....");
    if (QTEL_CheckSIMCard(hsim) == QTEL_OK) {
      QTEL_SetState(hsim, QTEL_STATE_CHECK_NETWORK);
      QTEL_Debug("SIM card OK");
    } else {
      QTEL_Debug("SIM card Not Ready");
      break;
    }
    break;

  case QTEL_STATE_CHECK_NETWORK:
    QTEL_Debug("Checking cellular network....");
    if (QTEL_CheckNetwork(hsim) == QTEL_OK) {
      QTEL_Debug("Cellular network registered", (hsim->network_status == 5)? " (roaming)":"");
      hsim->rtos.eventSet(QTEL_RTOS_EVT_NEW_STATE);
    }
    else if (hsim->network_status == 0) {
      QTEL_ReqisterNetwork(hsim);
    }
    else if (hsim->network_status == 2) {
      QTEL_Debug("Searching network....");
      break;
    }
    break;

  case QTEL_STATE_ACTIVE:
    hsim->rtos.eventSet(QTEL_RTOS_EVT_ACTIVED);
    break;

  default: break;
  }
}

static void loop(QTEL_HandlerTypeDef* hsim)
{
  switch (hsim->state) {
  case QTEL_STATE_NON_ACTIVE:
    if (QTEL_IsTimeout(hsim, hsim->tick.init, 3000)) {
      QTEL_SetState(hsim, QTEL_STATE_CHECK_AT);
    }
    break;

  case QTEL_STATE_READY:
    if (QTEL_IsTimeout(hsim, hsim->tick.changedState, 300)) {
      QTEL_SetState(hsim, QTEL_STATE_CHECK_AT);
    }
    break;

  case QTEL_STATE_CHECK_AT:
    if (QTEL_IsTimeout(hsim, hsim->tick.changedState, 1000)) {
      QTEL_SetState(hsim, QTEL_STATE_CHECK_SIMCARD);
    }
    break;

  case QTEL_STATE_CHECK_SIMCARD:
    if (QTEL_IsTimeout(hsim, hsim->tick.changedState, 1000)) {
      QTEL_SetState(hsim, QTEL_STATE_CHECK_SIMCARD);
    }
    break;

  case QTEL_STATE_CHECK_NETWORK:
    if (QTEL_IsTimeout(hsim, hsim->tick.changedState, 3000)) {
      QTEL_SetState(hsim, QTEL_STATE_CHECK_NETWORK);
    }
    break;

  case QTEL_STATE_ACTIVE:
    if (QTEL_IsTimeout(hsim, hsim->tick.checksignal, 3000)) {
      hsim->tick.checksignal = hsim->getTick();
      QTEL_CheckSugnal(hsim);
    }
    break;

  default: break;
  }
}

static void onReady(void *app, AT_Data_t *_)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;

  hsim->status  = 0;
  hsim->events  = 0;
  QTEL_Debug("Starting...");

  QTEL_SetState(hsim, QTEL_STATE_READY);
}


static void onGetRespConnect(void *app, uint8_t *data, uint16_t len)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;
  uint32_t events;
  uint8_t counter = 0;

  if (len > QTEL_RESP_CONNECT_BUFFER_SIZE) len = QTEL_RESP_CONNECT_BUFFER_SIZE;
  else qtelPtr->respConnectBuffer[len] = 0;

  memcpy(qtelPtr->respConnectBuffer, data, len);

  qtelPtr->rtos.eventClear(QTEL_RTOS_EVT_RESP_CONNECT_CLOSE);
  qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_RESP_CONNECT);

  while (qtelPtr->isRespConnectHandle) {
    if (qtelPtr->rtos.eventWait(QTEL_RTOS_EVT_RESP_CONNECT_CLOSE, &events, 1000) != AT_OK) {
      if (counter++ > 60) {
        // break if too long
        qtelPtr->isRespConnectHandle = 0;
        break;
      }
    }
  }
}
