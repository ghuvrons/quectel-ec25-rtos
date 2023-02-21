/*
 * net.h
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#include <quectel-ec25/net.h>
#if QTEL_EN_FEATURE_NET

#include "../events.h"
#include <quectel-ec25.h>
#include <quectel-ec25/gps.h>
#include <quectel-ec25/socket.h>
#include <quectel-ec25/utils.h>
#include <stdlib.h>
#include <string.h>


QTEL_Status_t QTEL_NET_Init(QTEL_NET_HandlerTypeDef *qtelNet, void *qtelPtr)
{
  if (((QTEL_HandlerTypeDef*)qtelPtr)->key != QTEL_KEY)
    return QTEL_ERROR;

  qtelNet->qtel         = qtelPtr;
  qtelNet->contextId    = 1;
  qtelNet->status       = 0;
  qtelNet->events       = 0;
  qtelNet->gprs_status  = 0;
  qtelNet->state        = QTEL_NET_STATE_NON_ACTIVE;

  return QTEL_OK;
}


void QTEL_NET_SetupAPN(QTEL_NET_HandlerTypeDef *qtelNet, char *APN, char *user, char *pass)
{
  qtelNet->APN.APN   = APN;
  qtelNet->APN.user  = 0;
  qtelNet->APN.pass  = 0;

  if (strlen(user) > 0)
    qtelNet->APN.user = user;
  if (strlen(pass) > 0)
    qtelNet->APN.pass = pass;
}


void QTEL_NET_SetState(QTEL_NET_HandlerTypeDef *qtelNet, uint8_t newState)
{
  qtelNet->state = newState;
  ((QTEL_HandlerTypeDef*) qtelNet->qtel)->rtos.eventSet(QTEL_RTOS_EVT_NET_NEW_STATE);
}


void QTEL_NET_OnNewState(QTEL_NET_HandlerTypeDef *qtelNet)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelNet->qtel;

  qtelNet->stateTick = qtelPtr->getTick();

  switch (qtelNet->state) {
  case QTEL_NET_STATE_CHECK_GPRS:
    QTEL_Debug("Checking GPRS...");
    if (QTEL_NET_GPRS_Check(qtelNet) == QTEL_OK) {
      QTEL_Debug("GPRS registered%s", (qtelNet->gprs_status == 5)? " (roaming)":"");

#if QTEL_EN_FEATURE_GPS
      if (qtelPtr->gps.state == QTEL_GPS_STATE_NON_ACTIVE) {
        QTEL_GPS_SetState(&qtelPtr->gps, QTEL_GPS_STATE_SETUP);
      }
#endif /* QTEL_EN_FEATURE_GPS */
    }
    else if (qtelPtr->network_status == 2) {
      QTEL_Debug("GPRS Registering....");
    }
    break;

  case QTEL_NET_STATE_SET_PDP_CONTEXT:
    if (qtelNet->APN.APN != NULL) {
      if (QTEL_NET_ConfigureContext(qtelNet, qtelNet->contextId) == QTEL_OK)
      {
        QTEL_Debug("APN was configured");
      }
      if (QTEL_NET_ActivateContext(qtelNet, qtelNet->contextId) == QTEL_OK)
      {
        QTEL_Debug("APS active");
      }
    }
    break;

  case QTEL_NET_STATE_ONLINE:
#if QTEL_EN_FEATURE_SOCKET
    QTEL_SockManager_OnNetOnline(&qtelPtr->socketManager);
#endif
    break;

  default: break;
  }

  return;
}


void QTEL_NET_Loop(QTEL_NET_HandlerTypeDef *qtelNet)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelNet->qtel;

  switch (qtelNet->state) {
  case QTEL_NET_STATE_CHECK_GPRS:
    if (QTEL_IsTimeout(qtelPtr, qtelNet->stateTick, 2000)) {
      QTEL_NET_SetState(qtelNet, QTEL_NET_STATE_CHECK_GPRS);
    }
    break;

  default: break;
  }

  return;
}


QTEL_Status_t QTEL_NET_GPRS_Check(QTEL_NET_HandlerTypeDef *qtelNet)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelNet->qtel;
  QTEL_Status_t status = QTEL_ERROR;

  uint8_t lac[2]; // location area code
  uint8_t ci[2];  // Cell Identify

  AT_Data_t respData[4] = {
    AT_Number(0),
    AT_Number(0),
    AT_Hex(lac),
    AT_Hex(ci),
  };

  memset(lac, 0, 2);
  memset(ci, 0, 2);

  if (AT_Check(&qtelPtr->atCmd, "+CGREG", 4, respData) != AT_OK) return status;
  qtelNet->gprs_status = (uint8_t) respData[1].value.number;

  // check response
  if (qtelNet->gprs_status == 1 || qtelNet->gprs_status == 5) {
    status = QTEL_OK;
    if (qtelNet->state <= QTEL_NET_STATE_CHECK_GPRS) {
      QTEL_NET_SetState(qtelNet, QTEL_NET_STATE_SET_PDP_CONTEXT);
    }
    if (qtelNet->gprs_status == 5)
      QTEL_SET_STATUS(qtelNet, QTEL_NET_STATUS_GPRS_ROAMING);
  }
  else {
    if (qtelNet->state > QTEL_NET_STATE_CHECK_GPRS)
      qtelNet->state = QTEL_NET_STATE_CHECK_GPRS;
    QTEL_UNSET_STATUS(qtelNet, QTEL_NET_STATUS_GPRS_ROAMING);
  }

  return status;
}


QTEL_Status_t QTEL_NET_ConfigureContext(QTEL_NET_HandlerTypeDef *qtelNet, uint8_t contextId)
{
  if (contextId == 0 || contextId > 16) return QTEL_ERROR;
  if (QTEL_BITS_IS_ALL(qtelNet->isCtxConfigured, (1 << (contextId-1)))) {
    return QTEL_OK;
  }

  QTEL_HandlerTypeDef *qtelPtr  = qtelNet->qtel;
  QTEL_Status_t status          = QTEL_ERROR;
  char *APN                     = qtelNet->APN.APN;
  char *user                    = qtelNet->APN.user;
  char *pass                    = qtelNet->APN.pass;

  AT_Data_t paramData[6] = {
    AT_Number(contextId),
    AT_String("IPV4V6"),
    AT_String(APN),
    AT_String(""),
    AT_String(""),
    AT_Number(0),
  };

  if (AT_Command(&qtelPtr->atCmd, "+CGDCONT", 3, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetNumber(&paramData[1], 3);
  if (user == NULL) {
    AT_DataSetNumber(&paramData[5], 0);
  }
  else {
    AT_DataSetString(&paramData[3], user);
    AT_DataSetNumber(&paramData[5], 3);

    if (pass != NULL)
      AT_DataSetString(&paramData[4], pass);

  }

  if (AT_Command(&qtelPtr->atCmd, "+QICSGP", 6, paramData, 0, 0) != AT_OK) goto endCmd;

  QTEL_BITS_SET(qtelNet->isCtxConfigured, (1 << (contextId-1)));
  status = QTEL_OK;

endCmd:
  return status;
}


QTEL_Status_t QTEL_NET_ActivateContext(QTEL_NET_HandlerTypeDef *qtelNet, uint8_t contextId)
{
  if (contextId > 16) return QTEL_ERROR;

  QTEL_Status_t status          = QTEL_ERROR;
  QTEL_HandlerTypeDef *qtelPtr  = qtelNet->qtel;
  uint8_t commandSent = 0;
  AT_Data_t respData[16][3];
  AT_Data_t paramData[1] = {
    AT_Number(contextId),
  };

checkContext:
  memset(respData, 0, sizeof(respData));

  // check
  if (AT_CheckWithMultResp(&qtelPtr->atCmd, "+QIACT", 16, 3, &respData[0][0]) != AT_OK) return QTEL_ERROR;
  for (uint8_t i = 0; i < 16; i++) {
    if (respData[i][0].type == AT_NUMBER
        && respData[i][0].value.number == contextId) {
      if (respData[i][1].value.number == 1)
      {
        status = QTEL_OK;
        QTEL_BITS_SET(qtelNet->isCtxActived, (1 << (contextId-1)));
        if (qtelNet->contextId == contextId) {
          QTEL_NET_SetState(qtelNet, QTEL_NET_STATE_ONLINE);
        }
        goto endCmd;
      }
      break;
    }
  }

  // command
  if (commandSent == 1 ||
      AT_CommandWithTimeout(&qtelPtr->atCmd, "+QIACT",
                            1, paramData, 0, 0, 150000) != AT_OK)
  {
    if (AT_CommandWithTimeout(&qtelPtr->atCmd, "+QIDEACT",
                              1, paramData, 0, 0, 40000) != AT_OK)
    {
      // [TODO]: Reset modem
    }

    goto endCmd;
  }
  commandSent = 1;
  goto checkContext;

endCmd:
  return status;
}

#endif /* QTEL_EN_FEATURE_NET */
