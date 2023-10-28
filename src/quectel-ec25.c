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
static void checkSIM(QTEL_HandlerTypeDef*);
static void checkNetwork(QTEL_HandlerTypeDef*);
static void checkGPRSNetwork(QTEL_HandlerTypeDef*);
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
  memset(simStatus, 0, sizeof(AT_Data_t));
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
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_NEW_STATE)) {
        onNewState(qtelPtr);
      }
      if (IS_EVENT(notifEvent, QTEL_RTOS_EVT_ACTIVED)) {
#if QTEL_EN_FEATURE_GPS
        if (qtelPtr->gps.state == QTEL_GPS_STATE_NON_ACTIVE) {
          QTEL_GPS_SetState(&qtelPtr->gps, QTEL_GPS_STATE_SETUP);
        }
#endif /* QTEL_EN_FEATURE_GPS */

#if QTEL_EN_FEATURE_SOCKET
        if (qtelPtr->socketManager.state == QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING) {
          QTEL_SockManager_SetState(&qtelPtr->socketManager, QTEL_SOCKH_STATE_PDP_ACTIVATING);
        }
#endif /* QTEL_EN_FEATURE_SOCKET */
        if (qtelPtr->callbacks.onActive) qtelPtr->callbacks.onActive();
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


void QTEL_SetState(QTEL_HandlerTypeDef *qtelPtr, QTEL_State_t newState)
{
  qtelPtr->state = newState;
  qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_NEW_STATE);
}

static void onNewState(QTEL_HandlerTypeDef *qtelPtr)
{
  uint8_t respstr[20];
  AT_Data_t paramData[1];
  AT_Data_t respData[2];
  uint8_t isNeedReset;
  AT_Status_t status;

  qtelPtr->tick.changedState = qtelPtr->getTick();

  switch (qtelPtr->state) {
  case QTEL_STATE_REBOOT:

#if QTEL_EN_FEATURE_SOCKET
    QTEL_SockManager_OnReboot(&qtelPtr->socketManager);
#endif /* QTEL_EN_FEATURE_SOCKET */

    if (qtelPtr->resetPower != 0) {
      while (qtelPtr->resetPower() != QTEL_OK) {
        qtelPtr->delay(1);
      }
    }
    QTEL_SetState(qtelPtr, QTEL_STATE_CHECK_AT);
    break;

  case QTEL_STATE_CONFIGURATION:
    isNeedReset = 0;

    // Get firmware
    // QTEL_GetFirmwareVersion(qtelPtr);

    // disable echo
    QTEL_Echo(qtelPtr, 0);

    //
    memset(respstr, 0, sizeof(respstr));
    AT_DataSetString(&paramData[0], "urcport");
    AT_DataSetBuffer(&respData[0], &respstr[0], 8);
    AT_DataSetBuffer(&respData[1], &respstr[8], 9);
    status = AT_Command(&qtelPtr->atCmd, "+QURCCFG", 1, paramData, 2, respData);
    if (status != AT_OK ||
        respData[1].type != AT_STRING ||
        (strncmp(respData[1].value.string, "uart1", 5)) != 0)
    {
      AT_Command(&qtelPtr->atCmd, "+QURCCFG=\"urcport\",\"uart1\"", 0, 0, 0, 0);
      isNeedReset = 1;
    }

    //
    AT_DataSetString(&paramData[0], "urc/poweron");
    AT_DataSetBuffer(&respData[0], &respstr[0], 12);
    AT_DataSetNumber(&respData[1], 1);
    status = AT_Command(&qtelPtr->atCmd, "+QCFG", 1, paramData, 2, respData);
    if (status != AT_OK ||
        respData[1].type != AT_NUMBER ||
        respData[1].value.number != 0)
    {
      AT_Command(&qtelPtr->atCmd, "+QCFG=\"urc/poweron\",0", 0, 0, 0, 0);
      isNeedReset = 1;
    }


    AT_Command(&qtelPtr->atCmd, "+CREG=1", 0, 0, 0, 0);
    AT_Command(&qtelPtr->atCmd, "+CGREG=1", 0, 0, 0, 0);
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_CONFIGURED);

    if (isNeedReset) QTEL_ResetSIM(qtelPtr);

    QTEL_SetState(qtelPtr, QTEL_STATE_CHECK_SIMCARD);

    if (qtelPtr->callbacks.onReady)
      qtelPtr->callbacks.onReady();

    break;

  case QTEL_STATE_CHECK_SIMCARD:
    if (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_SIM_READY))
    {
      QTEL_GetSIMInfo(qtelPtr);
      QTEL_SetState(qtelPtr, QTEL_STATE_CHECK_NETWORK);
    }
    else
    {
      checkSIM(qtelPtr);
    }
    break;

  case QTEL_STATE_CHECK_NETWORK:
    QTEL_Debug("Checking network....");
    if (!QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED)) {
      checkNetwork(qtelPtr);
    }

    if (!QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED)) {
      checkGPRSNetwork(qtelPtr);
    }

    if (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED) &&
        QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED))
    {
      QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
    }
    break;

  case QTEL_STATE_ACTIVE:
    qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_ACTIVED);
    QTEL_GetOperator(qtelPtr);
    QTEL_CheckQENG(qtelPtr);
    QTEL_Debug("operator: %s", qtelPtr->operator);
    QTEL_Debug("Active");
    break;

  default: break;
  }
}

static void loop(QTEL_HandlerTypeDef *qtelPtr)
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
      checkSIM(qtelPtr);
    }
    break;

  case QTEL_STATE_CHECK_NETWORK:
    if (QTEL_IsTimeout(qtelPtr, qtelPtr->tick.changedState, 120000)) {
      // reset timer
      qtelPtr->tick.changedState = qtelPtr->getTick();

      QTEL_Debug("Rechecking network....");
      if (!QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED)) {
        checkNetwork(qtelPtr);
      }

      if (!QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED)) {
        checkGPRSNetwork(qtelPtr);
      }

      if (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED) &&
          QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED))
      {
        QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
      }
      else {
        QTEL_SetState(qtelPtr, QTEL_STATE_REBOOT);
      }
    }
    break;

  case QTEL_STATE_ACTIVE:
    if (QTEL_IsTimeout(qtelPtr, qtelPtr->tick.checksignal, 10000)) {
      qtelPtr->tick.checksignal = qtelPtr->getTick();
      if (QTEL_CheckSugnal(qtelPtr) == QTEL_RESPONSE_TIMEOUT) {
        QTEL_SetState(qtelPtr, QTEL_STATE_REBOOT);
      }
    }
    break;

  default: break;
  }
}

static void checkSIM(QTEL_HandlerTypeDef *qtelPtr) {
  QTEL_Debug("Checking SIM....");
  if (QTEL_CheckSIMCard(qtelPtr) == QTEL_OK) {
    QTEL_GetSIMInfo(qtelPtr);
    if (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED) &&
        QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED))
    {
      QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
      return;
    }
    QTEL_SetState(qtelPtr, QTEL_STATE_CHECK_NETWORK);
  }
  else {
    QTEL_Debug("SIM Not Ready");
    return;
  }
}


static void checkNetwork(QTEL_HandlerTypeDef *qtelPtr)
{
  if (QTEL_CheckNetwork(qtelPtr) != QTEL_OK) {
    // TODO handle error
    return;
  }

  switch (qtelPtr->network_status)
  {
  case 1:
  case 5:
    QTEL_Debug("Cellular network registered", (qtelPtr->network_status == 5)? " (roaming)":"");
    break;
  
  case 0:
    QTEL_ReqisterNetwork(qtelPtr);
    break;

  case 2:
    QTEL_Debug("Searching network....");
    break;

  default:
    break;
  }
}


static void checkGPRSNetwork(QTEL_HandlerTypeDef *qtelPtr)
{
  if (QTEL_CheckGPRSNetwork(qtelPtr) != QTEL_OK) {
    // TODO handle error
    return;
  }

  switch (qtelPtr->GPRS_network_status)
  {
  case 1:
  case 5:
    QTEL_Debug("GPRS network registered", (qtelPtr->GPRS_network_status == 5)? " (roaming)":"");
    break;

  case 2:
    QTEL_Debug("Searching GPRS network....");
    break;

  default:
    break;
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

  if (strncmp(data->value.string, "READY", 5) == 0) {
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
  else {
    QTEL_Debug("SIM Not Ready");
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

  case 2:
    QTEL_Debug("Searching network....");

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
