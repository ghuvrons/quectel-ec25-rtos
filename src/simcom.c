/*
 * simcom.c
 *
 *  Created on: Nov 7, 2022
 *      Author: janoko
 */

#include "include/simcom.h"
#include "include/simcom/core.h"
#include "include/simcom/utils.h"
#include "events.h"
#include <stdlib.h>

#define IS_EVENT(evt_notif, evt_wait) SIM_BITS_IS(evt_notif, evt_wait)

static void onNewState(SIM_HandlerTypeDef*);
static void loop(SIM_HandlerTypeDef*);
static void onReady(void *app, AT_Data_t*);


SIM_Status_t SIM_Init(SIM_HandlerTypeDef *hsim)
{
  if (hsim->getTick == 0) return SIM_ERROR;
  if (hsim->delay == 0) return SIM_ERROR;

  hsim->atCmd.serial.readline = hsim->serial.readline;
  hsim->network_status = 0;

  hsim->atCmd.serial.read     = hsim->serial.read;
  hsim->atCmd.serial.readinto = hsim->serial.readinto;
  hsim->atCmd.serial.readline = hsim->serial.readline;
  hsim->atCmd.serial.write    = hsim->serial.write;

  hsim->atCmd.rtos.mutexLock    = hsim->rtos.mutexLock;
  hsim->atCmd.rtos.mutexUnlock  = hsim->rtos.mutexUnlock;
  hsim->atCmd.rtos.eventSet     = hsim->rtos.eventSet;
  hsim->atCmd.rtos.eventWait    = hsim->rtos.eventWait;
  hsim->atCmd.rtos.eventClear   = hsim->rtos.eventClear;

  AT_Config_t config;
  config.timeout = 30000; // ms

  if (AT_Init(&hsim->atCmd, &config) != AT_OK) return SIM_ERROR;

  AT_On(&hsim->atCmd, "RDY", hsim, 0, 0, onReady);

  hsim->key = SIM_KEY;

#if SIM_EN_FEATURE_NET
  SIM_NET_Init(&hsim->net, hsim);
#endif /* SIM_EN_FEATURE_NET */

#if SIM_EN_FEATURE_NTP
  SIM_NTP_Init(&hsim->ntp, hsim);
#endif /* SIM_EN_FEATURE_NTP */

#if SIM_EN_FEATURE_SOCKET
  SIM_SockManager_Init(&hsim->socketManager, hsim);
#endif /* SIM_EN_FEATURE_SOCKET */


#if SIM_EN_FEATURE_HTTP
  SIM_HTTP_Init(&hsim->http, hsim);
#endif /* SIM_EN_FEATURE_GPS */

#if SIM_EN_FEATURE_GPS
  SIM_GPS_Init(&hsim->gps, hsim);
#endif /* SIM_EN_FEATURE_GPS */

#if SIM_EN_FEATURE_FILE
  SIM_FILE_Init(&hsim->file, hsim);
#endif /* SIM_EN_FEATURE_GPS */

  hsim->tick.init = hsim->getTick();

  return SIM_OK;
}

// SIMCOM Application Threads
void SIM_Thread_Run(SIM_HandlerTypeDef *hsim)
{
  uint32_t notifEvent;
  uint32_t timeout = 2000; // ms
  uint32_t lastTO = 0;

  for (;;) {
    if (hsim->rtos.eventWait(SIM_RTOS_AVT_ALL, &notifEvent, timeout) == AT_OK) {
      if (IS_EVENT(notifEvent, SIM_RTOS_EVT_READY)) {

      }
      if (IS_EVENT(notifEvent, SIM_RTOS_EVT_NEW_STATE)) {
        onNewState(hsim);
      }
      if (IS_EVENT(notifEvent, SIM_RTOS_EVT_ACTIVED)) {
#if SIM_EN_FEATURE_NET
        SIM_NET_SetState(&hsim->net, SIM_NET_STATE_CHECK_GPRS);
#endif /* SIM_EN_FEATURE_NET */

#if SIM_EN_FEATURE_GPS
        SIM_GPS_SetState(&hsim->gps, SIM_GPS_STATE_SETUP);
#endif /* SIM_EN_FEATURE_GPS */
      }

#if SIM_EN_FEATURE_NET
      if (IS_EVENT(notifEvent, SIM_RTOS_EVT_NET_NEW_STATE)) {
        SIM_NET_OnNewState(&hsim->net);
      }
#endif /* SIM_EN_FEATURE_NET */

#if SIM_EN_FEATURE_SOCKET
      if (IS_EVENT(notifEvent, SIM_RTOS_EVT_SOCKMGR_NEW_STATE)) {
        SIM_SockManager_OnNewState(&hsim->socketManager);
      }
      if (IS_EVENT(notifEvent, SIM_RTOS_EVT_SOCKCLIENT_NEW_EVT)) {
        SIM_SockManager_CheckSocketsEvents(&hsim->socketManager);
      }
#endif /* SIM_EN_FEATURE_SOCKET */

#if SIM_EN_FEATURE_NTP
      if (IS_EVENT(notifEvent, SIM_RTOS_EVT_NTP_SYNCED)) {
        SIM_NTP_OnSynced(&hsim->ntp);
      }
#endif /* SIM_EN_FEATURE_NTP */

#if SIM_EN_FEATURE_GPS
      if (IS_EVENT(notifEvent, SIM_RTOS_EVT_GPS_NEW_STATE)) {
        SIM_GPS_OnNewState(&hsim->gps);
      }
#endif /* SIM_EN_FEATURE_GPS */

      goto next;
    }

    lastTO = hsim->getTick();
    loop(hsim);

#if SIM_EN_FEATURE_NET
    SIM_NET_Loop(&hsim->net);
#endif /* SIM_EN_FEATURE_NET */

#if SIM_EN_FEATURE_SOCKET
    SIM_SockManager_Loop(&hsim->socketManager);
#endif /* SIM_EN_FEATURE_SOCKET */

#if SIM_EN_FEATURE_NTP
    SIM_NTP_Loop(&hsim->ntp);
#endif /* SIM_EN_FEATURE_NTP */

  next:
    timeout = 1000 - (hsim->getTick() - lastTO);
    if (timeout > 1000) timeout = 1;
  }
}


// AT Command Threads
void SIM_Thread_ATCHandler(SIM_HandlerTypeDef *hsim)
{
  AT_Process(&hsim->atCmd);
}

void SIM_SetState(SIM_HandlerTypeDef *hsim, uint8_t newState)
{
  hsim->state = newState;
  hsim->rtos.eventSet(SIM_RTOS_EVT_NEW_STATE);
}

static void onNewState(SIM_HandlerTypeDef *hsim)
{
  hsim->tick.changedState = hsim->getTick();

  switch (hsim->state) {
  case SIM_STATE_NON_ACTIVE:
  case SIM_STATE_CHECK_AT:
    if (SIM_CheckAT(hsim) == SIM_OK) {
      SIM_SetState(hsim, SIM_STATE_CHECK_SIMCARD);
    }
    break;

  case SIM_STATE_CHECK_SIMCARD:
    SIM_Debug("Checking SIM Card....");
    if (SIM_CheckSIMCard(hsim) == SIM_OK) {
      SIM_SetState(hsim, SIM_STATE_CHECK_NETWORK);
      SIM_Debug("SIM card OK");
    } else {
      SIM_Debug("SIM card Not Ready");
      break;
    }
    break;

  case SIM_STATE_CHECK_NETWORK:
    SIM_Debug("Checking cellular network....");
    if (SIM_CheckNetwork(hsim) == SIM_OK) {
      SIM_Debug("Cellular network registered", (hsim->network_status == 5)? " (roaming)":"");
      hsim->rtos.eventSet(SIM_RTOS_EVT_NEW_STATE);
    }
    else if (hsim->network_status == 0) {
      SIM_ReqisterNetwork(hsim);
    }
    else if (hsim->network_status == 2) {
      SIM_Debug("Searching network....");
      break;
    }
    break;

  case SIM_STATE_ACTIVE:
    hsim->rtos.eventSet(SIM_RTOS_EVT_ACTIVED);
    break;

  default: break;
  }
}

static void loop(SIM_HandlerTypeDef* hsim)
{
  switch (hsim->state) {
  case SIM_STATE_NON_ACTIVE:
    if (SIM_IsTimeout(hsim, hsim->tick.init, 30000)) {
      SIM_SetState(hsim, SIM_STATE_CHECK_AT);
    }
    break;

  case SIM_STATE_CHECK_AT:
    if (SIM_IsTimeout(hsim, hsim->tick.changedState, 1000)) {
      SIM_SetState(hsim, SIM_STATE_CHECK_SIMCARD);
    }
    break;

  case SIM_STATE_CHECK_SIMCARD:
    if (SIM_IsTimeout(hsim, hsim->tick.changedState, 2000)) {
      SIM_SetState(hsim, SIM_STATE_CHECK_SIMCARD);
    }
    break;

  case SIM_STATE_CHECK_NETWORK:
    if (SIM_IsTimeout(hsim, hsim->tick.changedState, 3000)) {
      SIM_SetState(hsim, SIM_STATE_CHECK_NETWORK);
    }
    break;

  case SIM_STATE_ACTIVE:
    if (SIM_IsTimeout(hsim, hsim->tick.checksignal, 3000)) {
      hsim->tick.checksignal = hsim->getTick();
      SIM_CheckSugnal(hsim);
    }
    break;

  default: break;
  }
}

static void onReady(void *app, AT_Data_t *_)
{
  SIM_HandlerTypeDef *hsim = (SIM_HandlerTypeDef*)app;

  hsim->status  = 0;
  hsim->events  = 0;
  SIM_Debug("Starting...");

  SIM_SetState(hsim, SIM_STATE_CHECK_AT);
}
