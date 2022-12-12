/*
 * core.c
 *
 *  Created on: Nov 8, 2022
 *      Author: janoko
 */

#include "include/simcom.h"
#include "include/simcom/core.h"
#include "include/simcom/utils.h"
#include <stdlib.h>


static void str2Time(SIM_Datetime_t*, const char *);

SIM_Status_t SIM_CheckAT(SIM_HandlerTypeDef *hsim)
{
  SIM_Status_t status = SIM_ERROR;

  if (AT_Command(&hsim->atCmd, "", 0, 0, 0, 0) == AT_OK) {
    status = SIM_OK;
    if (hsim->state <= SIM_STATE_CHECK_AT) {
      hsim->state = SIM_STATE_CHECK_AT+1;
    }
    SIM_Echo(hsim, 0);
    SIM_SET_STATUS(hsim, SIM_STATUS_ACTIVE);
  } else {
    hsim->state = SIM_STATE_CHECK_AT;
    SIM_UNSET_STATUS(hsim, SIM_STATUS_ACTIVE);
  }

  return status;
}


SIM_Status_t SIM_Echo(SIM_HandlerTypeDef *hsim, uint8_t onoff)
{
  SIM_Status_t status = SIM_ERROR;

  if (AT_Command(&hsim->atCmd, (onoff)? "E1": "E0", 0, 0, 0, 0) == AT_OK) {
    status = SIM_OK;
  }

  return status;
}


SIM_Status_t SIM_CheckSIMCard(SIM_HandlerTypeDef *hsim)
{
  SIM_Status_t status = SIM_ERROR;
  uint8_t respstr[6];
  AT_Data_t respData[1] = {
    AT_Buffer(respstr, sizeof(respstr)),
  };

  memset(respstr, 0, 6);

  AT_Check(&hsim->atCmd, "+CPIN", 1, respData);
  if (strncmp(respData[0].value.string, "READY", 5) == 0) {
    status = SIM_OK;
  }
  return status;
}


SIM_Status_t SIM_CheckNetwork(SIM_HandlerTypeDef *hsim)
{
  SIM_Status_t status = SIM_ERROR;
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

  if (AT_Check(&hsim->atCmd, "+CREG", 4, respData) != AT_OK) return status;
  hsim->network_status = (uint8_t) respData[1].value.number;

  // check response
  if (hsim->network_status == 1 || hsim->network_status == 5) {
    status = SIM_OK;
    if (hsim->state <= SIM_STATE_CHECK_NETWORK) {
      SIM_SetState(hsim, SIM_STATE_ACTIVE);
    }
    if (hsim->network_status == 5)
      SIM_SET_STATUS(hsim, SIM_STATUS_ROAMING);
  }
  else {
    if (hsim->state > SIM_STATE_CHECK_NETWORK)
      hsim->state = SIM_STATE_CHECK_NETWORK;
    SIM_UNSET_STATUS(hsim, SIM_STATUS_ROAMING);
  }

  return status;
}


SIM_Status_t SIM_ReqisterNetwork(SIM_HandlerTypeDef *hsim)
{
  SIM_Status_t status = SIM_ERROR;
  uint8_t operator_selection_mode;
  AT_Data_t paramData[1] = {
      AT_Number(0),
  };
  uint8_t respstr[32];
  AT_Data_t respData[4] = {
    AT_Number(0),
    AT_Number(0),
    AT_Buffer(respstr, 32),
    AT_Number(0),
  };

  memset(respstr, 0, 32);

  SIM_Debug("Registering cellular network....");

  // Select operator automatically
  if (AT_Check(&hsim->atCmd, "+COPS", 1, respData) != AT_OK) goto endCmd;
  operator_selection_mode = (uint8_t) respData[0].value.number;

  if (operator_selection_mode != 0) {
    // set
    if (AT_Command(&hsim->atCmd, "+COPS", 1, paramData, 0, 0) != AT_OK) goto endCmd;

    // run
    if (AT_Command(&hsim->atCmd, "+COPS", 0, 0, 0, 0) != AT_OK) goto endCmd;
  }

endCmd:
  return status;
}


SIM_Status_t SIM_GetTime(SIM_HandlerTypeDef *hsim, SIM_Datetime_t *dt)
{
  uint8_t respstr[24];
  AT_Data_t respData[1] = {
    AT_Buffer(respstr, 24),
  };
  memset(respstr, 0, 24);

  if (AT_Check(&hsim->atCmd, "+CCLK",  1, respData) != AT_OK) return SIM_ERROR;

  str2Time(dt, (char*)&respstr[0]);

  return SIM_OK;
}


SIM_Status_t SIM_CheckSugnal(SIM_HandlerTypeDef *hsim)
{
  AT_Data_t respData[2] = {
    AT_Number(0),
    AT_Number(0),
  };

  if (AT_Command(&hsim->atCmd, "+CSQ", 0, 0, 2, respData) != AT_OK) return SIM_ERROR;
  hsim->signal = respData[1].value.number;

  return SIM_OK;
}


static void str2Time(SIM_Datetime_t *dt, const char *str)
{
  uint8_t *dtbytes = (uint8_t*) dt;
  int8_t mult = 1;
  uint8_t len = (uint8_t) sizeof(SIM_Datetime_t);
  uint8_t isParsing = 0;

  while (*str && len > 0) {
    if ((*str > '0' && *str < '9')) {
      if (!isParsing) {
        isParsing = 1;
        *dtbytes = ((int8_t) atoi(str)) * mult;
        dtbytes++;
        len--;
      }
    }
    else {
      isParsing = 0;
      if (*str == '-') {
        mult = -1;
      } else {
        mult = 1;
      }
    }

    str++;
  }
}
