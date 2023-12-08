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

  qtelNet->qtel = qtelPtr;

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


QTEL_Status_t QTEL_NET_ConfigurePDP(QTEL_NET_HandlerTypeDef *qtelNet, uint8_t contextId)
{
  if (contextId == 0 || contextId > 16) return QTEL_ERROR;
  if (QTEL_BITS_IS(qtelNet->isCtxConfigured, (1 << (contextId-1)))) {
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


QTEL_Status_t QTEL_NET_IsPDPActive(QTEL_NET_HandlerTypeDef *qtelNet, uint8_t contextId, uint8_t *isActive)
{
  if (contextId > 16) return QTEL_ERROR;

  AT_Status_t status;
  QTEL_HandlerTypeDef *qtelPtr  = qtelNet->qtel;
  AT_Data_t respData[16][3];

  memset(respData, 0, sizeof(respData));

  QTEL_Debug("check PDP Ctx %d", contextId);

  // check
  status = AT_CheckWithMultResp(&qtelPtr->atCmd, "+QIACT", 16, 3, &respData[0][0]);
  if (status != AT_OK) {
    return (QTEL_Status_t) status;
  }

  *isActive = 0;
  for (uint8_t i = 0; i < 16; i++) {
    if (respData[i][0].type == AT_NUMBER &&
        respData[i][0].value.number == contextId)
    {
      if (respData[i][1].value.number == 1) {
        *isActive = 1;
        QTEL_Debug("check PDP Ctx %d actived", contextId);
      }
      break;
    }
  }
  return QTEL_OK;
}


QTEL_Status_t QTEL_NET_ActivatePDP(QTEL_NET_HandlerTypeDef *qtelNet, uint8_t contextId)
{
  if (contextId > 16) return QTEL_ERROR;

  AT_Status_t atstatus;
  QTEL_Status_t status          = QTEL_ERROR;
  QTEL_HandlerTypeDef *qtelPtr  = qtelNet->qtel;
  uint8_t commandSent = 0;
  uint8_t isActive = 0;
  AT_Data_t paramData[1] = {
    AT_Number(contextId),
  };

  if (qtelPtr->net.APN.APN != NULL) {
    QTEL_NET_ConfigurePDP(qtelNet, contextId);
  }

checkContext:

  // check
  status = QTEL_NET_IsPDPActive(qtelNet, contextId, &isActive);
  if (status != QTEL_OK) return status;
  if (isActive) return QTEL_OK;

  commandSent++;
  if (commandSent > 3) {
    QTEL_Reboot(qtelPtr);
    return QTEL_ERROR;
  }
  else if (commandSent > 2) {
    QTEL_Debug("trouble activate PDP Ctx %d", contextId);
  }

  QTEL_Debug("activate PDP Ctx %d", contextId);

  // command
  atstatus = AT_CommandWithTimeout(&qtelPtr->atCmd, "+QIACT",
                                   1, paramData, 0, 0, 150000);

  if (atstatus == AT_OK)
    goto checkContext;

  if (atstatus == AT_RESPONSE_TIMEOUT) {
    QTEL_Debug("activate PDP Ctx %d timeout", contextId);
    QTEL_Reboot(qtelPtr);
    return QTEL_ERROR;
  }

  atstatus = AT_CommandWithTimeout(&qtelPtr->atCmd, "+QIDEACT",
                                   1, paramData, 0, 0, 40000);

  if (atstatus == AT_RESPONSE_TIMEOUT) {
    QTEL_Debug("deactivate PDP Ctx %d timeout", contextId);
    QTEL_Reboot(qtelPtr);
    return QTEL_ERROR;
  }

  goto checkContext;
}


QTEL_Status_t QTEL_NET_DeactivatePDP(QTEL_NET_HandlerTypeDef *qtelNet, uint8_t contextId)
{
  if (contextId > 16) return QTEL_ERROR;

  AT_Status_t atstatus;
  QTEL_Status_t status          = QTEL_ERROR;
  QTEL_HandlerTypeDef *qtelPtr  = qtelNet->qtel;
  uint8_t commandSent = 0;
  uint8_t isActive = 0;
  AT_Data_t paramData[1] = {
    AT_Number(contextId),
  };

checkContext:
  status = QTEL_NET_IsPDPActive(qtelNet, contextId, &isActive);
  if (status != QTEL_OK) return status;
  if (!isActive) return QTEL_OK;

  commandSent++;
  if (commandSent > 3) {
    QTEL_Reboot(qtelPtr);
    return QTEL_ERROR;
  } else if  (commandSent > 2) {
    QTEL_Debug("check n %d", commandSent);
  }

  // command
  atstatus = AT_CommandWithTimeout(&qtelPtr->atCmd, "+QIDEACT",
                                   1, paramData, 0, 0, 40000);
  if (atstatus == AT_RESPONSE_TIMEOUT) {
    QTEL_Reboot(qtelPtr);
    return QTEL_ERROR;
  }
  goto checkContext;
}


QTEL_Status_t QTEL_NET_DataCounterReset(QTEL_NET_HandlerTypeDef *qtelNet)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelNet->qtel;

  if (AT_Command(&qtelPtr->atCmd, "+QGDCNT=0", 0, 0, 0, 0) != AT_OK) return QTEL_ERROR;
  return QTEL_OK;
}


QTEL_Status_t QTEL_NET_GetDataCounter(QTEL_NET_HandlerTypeDef *qtelNet, uint32_t *sent, uint32_t *recv)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelNet->qtel;

  AT_Data_t respData[2] = {
    AT_Number(0),
    AT_Number(0),
  };

  if (qtelPtr->state != QTEL_STATE_ACTIVE) return QTEL_ERROR;

  if (AT_Check(&qtelPtr->atCmd, "+QGDCNT", 2, respData) != AT_OK) return QTEL_ERROR;

  *sent = respData[0].value.number;
  *recv = respData[1].value.number;

  return QTEL_OK;
}

#endif /* QTEL_EN_FEATURE_NET */
