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
static void onSIMReady(void *app, AT_Data_t*);
static void onNetworkStatusUpdated(void *app, AT_Data_t*);
static void onGPRSNetworkStatusUpdated(void *app, AT_Data_t*);
static void onGetRespConnect(void *app, uint8_t *data, uint16_t len);

QTEL_Status_t QTEL_Init(QTEL_HandlerTypeDef *qtelPtr)
{
  if (qtelPtr->getTick == 0) return QTEL_ERROR;
  if (qtelPtr->delay == 0) return QTEL_ERROR;

  qtelPtr->atCmd.serial.readline = qtelPtr->serial.readline;
  qtelPtr->network_status = 0;
  qtelPtr->isRespConnectHandle = 0;

  qtelPtr->atCmd.serial.read     = qtelPtr->serial.read;
  qtelPtr->atCmd.serial.readinto = qtelPtr->serial.readinto;
  qtelPtr->atCmd.serial.readline = qtelPtr->serial.readline;
  qtelPtr->atCmd.serial.write    = qtelPtr->serial.write;

  qtelPtr->atCmd.rtos.mutexLock    = qtelPtr->rtos.mutexLock;
  qtelPtr->atCmd.rtos.mutexUnlock  = qtelPtr->rtos.mutexUnlock;
  qtelPtr->atCmd.rtos.eventSet     = qtelPtr->rtos.eventSet;
  qtelPtr->atCmd.rtos.eventWait    = qtelPtr->rtos.eventWait;
  qtelPtr->atCmd.rtos.eventClear   = qtelPtr->rtos.eventClear;

  AT_Config_t config = {
      .commandTimeout = 30000,
      .checkTimeout = 500,
  };

  if (AT_Init(&qtelPtr->atCmd, &config) != AT_OK) return QTEL_ERROR;

  AT_On(&qtelPtr->atCmd, "RDY", qtelPtr, 0, 0, onReady);

  uint8_t *simStatusStr = malloc(5);
  AT_Data_t *simStatus = malloc(sizeof(AT_Data_t));
  memset(simStatusStr, 0, 5);
  AT_DataSetBuffer(simStatus, simStatusStr, 16);
  AT_On(&qtelPtr->atCmd, "+CPIN", qtelPtr, 1, simStatus, onSIMReady);

  AT_ReadlineOn(&qtelPtr->atCmd, "CONNECT", qtelPtr, onGetRespConnect);

  AT_Data_t *networkStatusUpdatedResp = malloc(sizeof(AT_Data_t));
  memset(networkStatusUpdatedResp, 0, sizeof(AT_Data_t));
  AT_On(&qtelPtr->atCmd, "+CREG", qtelPtr, 
        1, networkStatusUpdatedResp, onNetworkStatusUpdated);

  AT_Data_t *gprsNetworkStatusUpdatedResp = malloc(sizeof(AT_Data_t));
  memset(gprsNetworkStatusUpdatedResp, 0, sizeof(AT_Data_t));
  AT_On(&qtelPtr->atCmd, "+CGREG", qtelPtr, 
        1, gprsNetworkStatusUpdatedResp, onGPRSNetworkStatusUpdated);

  qtelPtr->key = QTEL_KEY;

#if QTEL_EN_FEATURE_NET
  QTEL_NET_Init(&qtelPtr->net, qtelPtr);
#endif /* QTEL_EN_FEATURE_NET */

#if QTEL_EN_FEATURE_NTP
  QTEL_NTP_Init(&qtelPtr->ntp, qtelPtr);
#endif /* QTEL_EN_FEATURE_NTP */

#if QTEL_EN_FEATURE_SOCKET
  QTEL_SockManager_Init(&qtelPtr->socketManager, qtelPtr);
#endif /* QTEL_EN_FEATURE_SOCKET */


#if QTEL_EN_FEATURE_HTTP
  QTEL_HTTP_Init(&qtelPtr->http, qtelPtr);
#endif /* QTEL_EN_FEATURE_GPS */

#if QTEL_EN_FEATURE_GPS
  QTEL_GPS_Init(&qtelPtr->gps, qtelPtr);
#endif /* QTEL_EN_FEATURE_GPS */

#if QTEL_EN_FEATURE_FILE
  QTEL_FILE_Init(&qtelPtr->file, qtelPtr);
#endif /* QTEL_EN_FEATURE_GPS */

  qtelPtr->tick.init = qtelPtr->getTick();

  return QTEL_OK;
}

// SIMCOM Application Threads
void QTEL_Thread_Run(QTEL_HandlerTypeDef *qtelPtr)
{
  uint32_t notifEvent;
  uint32_t timeout = 2000; // ms
  uint32_t lastTO = 0;

  if (qtelPtr->resetPower != 0) {
    while (qtelPtr->resetPower() != QTEL_OK) {
      qtelPtr->delay(1);
    }
  }

  for (;;) {
    if (qtelPtr->rtos.eventWait(QTEL_RTOS_EVT_ALL, &notifEvent, timeout) == AT_OK) {
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_READY)) {

      }
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_NEW_STATE)) {
        onNewState(qtelPtr);
      }
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_ACTIVED)) {
#if QTEL_EN_FEATURE_GPS
        if (qtelPtr->gps.state == QTEL_GPS_STATE_NON_ACTIVE) {
          QTEL_GPS_SetState(&qtelPtr->gps, QTEL_GPS_STATE_SETUP);
        }
#endif /* QTEL_EN_FEATURE_GPS */
      }

#if QTEL_EN_FEATURE_SOCKET
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_SOCKH_NEW_EVT)) {
        QTEL_SockManager_OnNewState(&qtelPtr->socketManager);
      }
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT)) {
        QTEL_SockManager_CheckSocketsEvents(&qtelPtr->socketManager);
      }
#endif /* QTEL_EN_FEATURE_SOCKET */

#if QTEL_EN_FEATURE_NTP
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_NTP_SYNCED)) {
        QTEL_NTP_OnSyncingFinish(&qtelPtr->ntp);
      }
#endif /* QTEL_EN_FEATURE_NTP */

#if QTEL_EN_FEATURE_GPS
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_GPS_NEW_STATE)) {
        QTEL_GPS_OnNewState(&qtelPtr->gps);
      }
#endif /* QTEL_EN_FEATURE_GPS */

      goto next;
    }

    lastTO = qtelPtr->getTick();
    loop(qtelPtr);

#if QTEL_EN_FEATURE_SOCKET
    QTEL_SockManager_Loop(&qtelPtr->socketManager);
#endif /* QTEL_EN_FEATURE_SOCKET */

#if QTEL_EN_FEATURE_NTP
    QTEL_NTP_Loop(&qtelPtr->ntp);
#endif /* QTEL_EN_FEATURE_NTP */

#if QTEL_EN_FEATURE_GPS
    QTEL_GPS_Loop(&qtelPtr->gps);
#endif /* QTEL_EN_FEATURE_GPS */

  next:
    timeout = 1000 - (qtelPtr->getTick() - lastTO);
    if (timeout > 1000) timeout = 1;
  }
}


// AT Command Threads
void QTEL_Thread_ATCHandler(QTEL_HandlerTypeDef *qtelPtr)
{
  AT_Process(&qtelPtr->atCmd);
}


QTEL_Status_t QTEL_Start(QTEL_HandlerTypeDef *qtelPtr)
{
  return QTEL_OK;
}


QTEL_Status_t QTEL_ResetSIM(QTEL_HandlerTypeDef *qtelPtr)
{
  // disable sim
  if (AT_Command(&qtelPtr->atCmd, "+CFUN=0", 0, 0, 0, 0) != AT_OK) {
    return QTEL_ERROR;
  }

  QTEL_UNSET_STATUS(qtelPtr, QTEL_STATUS_SIM_READY);

  // enable sim
  if (AT_Command(&qtelPtr->atCmd, "+CFUN=1", 0, 0, 0, 0) != AT_OK) {
    return QTEL_ERROR;
  }
  return QTEL_OK;
}


void QTEL_SetState(QTEL_HandlerTypeDef *qtelPtr, uint8_t newState)
{
  qtelPtr->state = newState;
  qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_NEW_STATE);
}

static void onNewState(QTEL_HandlerTypeDef *qtelPtr)
{
  qtelPtr->tick.changedState = qtelPtr->getTick();

  switch (qtelPtr->state) {
  case QTEL_STATE_CONFIGURATION:
    QTEL_Echo(qtelPtr, 0);
    AT_Command(&qtelPtr->atCmd, "+QURCCFG=\"urcport\",\"uart1\"", 0, 0, 0, 0);
    AT_Command(&qtelPtr->atCmd, "+QCFG=\"urc/poweron\",0", 0, 0, 0, 0);
    AT_Command(&qtelPtr->atCmd, "+CREG=1", 0, 0, 0, 0);
    AT_Command(&qtelPtr->atCmd, "+CGREG=1", 0, 0, 0, 0);
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_CONFIGURED);
    QTEL_ResetSIM(qtelPtr);
    QTEL_SetState(qtelPtr, QTEL_STATE_CHECK_SIMCARD);
    break;

  case QTEL_STATE_CHECK_SIMCARD:
    if (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_SIM_READY))
    {
      QTEL_SetState(qtelPtr, QTEL_STATE_CHECK_NETWORK);
    }
    break;

  case QTEL_STATE_CHECK_NETWORK:
    if (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED) &&
        QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED))
    {
      QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
    }
    break;

  case QTEL_STATE_ACTIVE:
    qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_ACTIVED);
    QTEL_Debug("Actived");
    break;

  default: break;
  }
}

static void loop(QTEL_HandlerTypeDef* qtelPtr)
{
  switch (qtelPtr->state) {
  case QTEL_STATE_NON_ACTIVE:
    if (QTEL_IsTimeout(qtelPtr, qtelPtr->tick.init, 15000)) {
      // reset timer
      qtelPtr->tick.changedState = qtelPtr->getTick();

      goto checkAT;
    }
    break;

  case QTEL_STATE_READY:
    if (QTEL_IsTimeout(qtelPtr, qtelPtr->tick.changedState, 30000)) {
      // reset timer
      qtelPtr->tick.changedState = qtelPtr->getTick();

      goto checkAT;
    }
    break;

  case QTEL_STATE_CHECK_AT:
    if (QTEL_IsTimeout(qtelPtr, qtelPtr->tick.changedState, 1000)) {
      // reset timer
      qtelPtr->tick.changedState = qtelPtr->getTick();

      checkAT:
      if (QTEL_CheckAT(qtelPtr) == QTEL_OK) {
        if (!QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_CONFIGURED)) {
          QTEL_SetState(qtelPtr, QTEL_STATE_CONFIGURATION);
          break;
        }
        QTEL_SetState(qtelPtr, QTEL_STATE_CHECK_SIMCARD);
      }
    }
    break;

  case QTEL_STATE_CHECK_SIMCARD:
    if (QTEL_IsTimeout(qtelPtr, qtelPtr->tick.changedState, 10000)) {
      // reset timer
      qtelPtr->tick.changedState = qtelPtr->getTick();

      QTEL_Debug("Checking SIM Card....");
      if (QTEL_CheckSIMCard(qtelPtr) == QTEL_OK) {
        if (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED) &&
            QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED))
        {
          QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
          break;
        }
        QTEL_SetState(qtelPtr, QTEL_STATE_CHECK_NETWORK);
      }
      else {
        QTEL_Debug("SIM card Not Ready");
        break;
      }
    }
    break;

  case QTEL_STATE_CHECK_NETWORK:
    if (QTEL_IsTimeout(qtelPtr, qtelPtr->tick.changedState, 30000)) {
      // reset timer
      qtelPtr->tick.changedState = qtelPtr->getTick();

      QTEL_Debug("Checking network....");
      if (!QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED)) {
        if (QTEL_CheckNetwork(qtelPtr) != QTEL_OK) {
          if (qtelPtr->network_status == 0) {
            QTEL_ReqisterNetwork(qtelPtr);
          }
          else if (qtelPtr->network_status == 2) {
            QTEL_Debug("Searching network....");
          }
          break;
        }

        QTEL_Debug("Cellular network registered", (qtelPtr->network_status == 5)? " (roaming)":"");
      }

      if (!QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED)) {
        if (QTEL_CheckGPRSNetwork(qtelPtr) != QTEL_OK) {
          if (qtelPtr->GPRS_network_status == 0) {
            QTEL_ReqisterNetwork(qtelPtr);
          }
          else if (qtelPtr->GPRS_network_status == 2) {
            QTEL_Debug("Searching GPRS network....");
          }
          break;
        }
        QTEL_Debug("GPRS network registered", (qtelPtr->GPRS_network_status == 5)? " (roaming)":"");
      }

      if (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED) &&
          QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED))
      {
        QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
      }
    }
    break;

  case QTEL_STATE_ACTIVE:
    if (QTEL_IsTimeout(qtelPtr, qtelPtr->tick.checksignal, 10000)) {
      qtelPtr->tick.checksignal = qtelPtr->getTick();
      QTEL_CheckSugnal(qtelPtr);
    }
    break;

  default: break;
  }
}

static void onReady(void *app, AT_Data_t *_)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;

  qtelPtr->status  = 0;
  qtelPtr->events  = 0;
  QTEL_Debug("Starting...");

  QTEL_SetState(qtelPtr, QTEL_STATE_CONFIGURATION);
}


static void onSIMReady(void *app, AT_Data_t *data)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;

  if (strncmp(data->value.string, "READY", 5)) {
    QTEL_Debug("SIM Ready...");
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_SIM_READY);
    if (qtelPtr->state == QTEL_STATE_CHECK_SIMCARD) {
      if (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED) &&
          QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED))
      {
        QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
      }
      QTEL_SetState(qtelPtr, QTEL_STATE_CHECK_NETWORK);
    }
  }

  memset(data->ptr, 0, 5);
}


static void onNetworkStatusUpdated(void *app, AT_Data_t *data)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;

  if (data->type != AT_NUMBER) return;
  qtelPtr->network_status = data->value.number;

  switch (qtelPtr->network_status) {
  case 5:
  case 1:
    if (qtelPtr->state == QTEL_STATE_CHECK_NETWORK) {
      if (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED))
      {
        QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
      }
    }
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED);
    break;

  case 0:
    QTEL_ReqisterNetwork(qtelPtr);

  case 2:
    QTEL_Debug("Searching network....");

  default:
    if (qtelPtr->state > QTEL_STATE_CHECK_NETWORK)
      qtelPtr->state = QTEL_STATE_CHECK_NETWORK;
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED);
    break;
  }
}


static void onGPRSNetworkStatusUpdated(void *app, AT_Data_t *data)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;

  if (data->type != AT_NUMBER) return;
  qtelPtr->GPRS_network_status = data->value.number;

  switch (qtelPtr->GPRS_network_status) {
  case 5:
  case 1:
    if (qtelPtr->state == QTEL_STATE_CHECK_NETWORK) {
      if (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED)) {
        QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
      }
    }
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED);
    break;

  default:
    if (qtelPtr->state > QTEL_STATE_CHECK_NETWORK)
      qtelPtr->state = QTEL_STATE_CHECK_NETWORK;
    QTEL_UNSET_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED);
    break;
  }
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
