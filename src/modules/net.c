/*
 * net.h
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#include <quectel-ec25/net.h>
#if QTEL_EN_FEATURE_NET

#include "../events.h"
#include "../include/quectel-ec25.h"
#include <quectel-ec25/utils.h>
#include <stdlib.h>
#include <string.h>


QTEL_Status_t QTEL_NET_Init(QTEL_NET_HandlerTypeDef *hsimnet, void *hsim)
{
  if (((QTEL_HandlerTypeDef*)hsim)->key != QTEL_KEY)
    return QTEL_ERROR;

  hsimnet->hsim         = hsim;
  hsimnet->status       = 0;
  hsimnet->events       = 0;
  hsimnet->gprs_status  = 0;
  hsimnet->state        = QTEL_NET_STATE_NON_ACTIVE;

  return QTEL_OK;
}


void QTEL_NET_SetupAPN(QTEL_NET_HandlerTypeDef *hsimnet, char *APN, char *user, char *pass)
{
  hsimnet->APN.APN   = APN;
  hsimnet->APN.user  = 0;
  hsimnet->APN.pass  = 0;

  if (strlen(user) > 0)
    hsimnet->APN.user = user;
  if (strlen(pass) > 0)
    hsimnet->APN.pass = pass;
}


void QTEL_NET_SetState(QTEL_NET_HandlerTypeDef *hsimnet, uint8_t newState)
{
  hsimnet->state = newState;
  ((QTEL_HandlerTypeDef*) hsimnet->hsim)->rtos.eventSet(QTEL_RTOS_EVT_NET_NEW_STATE);
}


void QTEL_NET_OnNewState(QTEL_NET_HandlerTypeDef *hsimnet)
{
  QTEL_HandlerTypeDef *hsim = hsimnet->hsim;

  hsimnet->stateTick = hsim->getTick();

  switch (hsimnet->state) {
  case QTEL_NET_STATE_SETUP_APN:
    if (hsimnet->APN.APN != NULL) {
      if (QTEL_NET_SetAPN(hsimnet) == QTEL_OK)
      {
        QTEL_Debug("APS was set");
      }
    }
    QTEL_NET_SetState(hsimnet, QTEL_NET_STATE_CHECK_GPRS);
    break;

  case QTEL_NET_STATE_CHECK_GPRS:
    if (!QTEL_IS_STATUS(hsimnet, QTEL_NET_STATUS_APN_WAS_SET)) {
      QTEL_NET_SetState(hsimnet, QTEL_NET_STATE_SETUP_APN);
      break;
    }
    QTEL_Debug("Checking GPRS...");
    if (QTEL_NET_GPRS_Check(hsimnet) == QTEL_OK) {
      QTEL_Debug("GPRS registered%s", (hsimnet->gprs_status == 5)? " (roaming)":"");
    }
    else if (hsimnet->gprs_status == 0) {
      QTEL_NET_SetState(hsimnet, QTEL_NET_STATE_SETUP_APN);
    }
    else if (hsim->network_status == 2) {
      QTEL_Debug("GPRS Registering....");
    }
    break;

  default: break;
  }

  return;
}


void QTEL_NET_Loop(QTEL_NET_HandlerTypeDef *hsimnet)
{
  QTEL_HandlerTypeDef *hsim = hsimnet->hsim;

  switch (hsimnet->state) {
  case QTEL_NET_STATE_CHECK_GPRS:
    if (QTEL_IsTimeout(hsim, hsimnet->stateTick, 2000)) {
      QTEL_NET_SetState(hsimnet, QTEL_NET_STATE_CHECK_GPRS);
    }
    break;

  default: break;
  }

  return;
}


QTEL_Status_t QTEL_NET_GPRS_Check(QTEL_NET_HandlerTypeDef *hsimnet)
{
  QTEL_HandlerTypeDef *hsim = hsimnet->hsim;
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

  if (AT_Check(&hsim->atCmd, "+CGREG", 4, respData) != AT_OK) return status;
  hsimnet->gprs_status = (uint8_t) respData[1].value.number;

  // check response
  if (hsimnet->gprs_status == 1 || hsimnet->gprs_status == 5) {
    status = QTEL_OK;
    if (hsimnet->state <= QTEL_NET_STATE_CHECK_GPRS) {
      QTEL_NET_SetState(hsimnet, QTEL_NET_STATE_ONLINE);
    }
    if (hsimnet->gprs_status == 5)
      QTEL_SET_STATUS(hsimnet, QTEL_NET_STATUS_GPRS_ROAMING);
  }
  else {
    if (hsimnet->state > QTEL_NET_STATE_CHECK_GPRS)
      hsimnet->state = QTEL_NET_STATE_CHECK_GPRS;
    QTEL_UNSET_STATUS(hsimnet, QTEL_NET_STATUS_GPRS_ROAMING);
  }

  return status;
}


QTEL_Status_t QTEL_NET_SetAPN(QTEL_NET_HandlerTypeDef *hsimnet)
{
  QTEL_HandlerTypeDef *hsim = hsimnet->hsim;
  QTEL_Status_t status = QTEL_ERROR;
  char *APN = hsimnet->APN.APN;
  char *user = hsimnet->APN.user;
  char *pass = hsimnet->APN.pass;
  uint8_t cid = 1;
  AT_Data_t paramData[6] = {
    AT_Number(cid),
    AT_String("IPV4V6"),
    AT_String(APN),
    AT_String(""),
    AT_String(""),
    AT_Number(0),
  };

  if (AT_Command(&hsim->atCmd, "+CGDCONT", 3, paramData, 0, 0) != AT_OK) goto endCmd;

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

  if (AT_Command(&hsim->atCmd, "+QICSGP", 6, paramData, 0, 0) != AT_OK) goto endCmd;

  QTEL_SET_STATUS(hsimnet, QTEL_NET_STATUS_APN_WAS_SET);
  status = QTEL_OK;

endCmd:
  return status;
}


#endif /* QTEL_EN_FEATURE_NET */
