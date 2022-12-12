/*
 * core.c
 *
 *  Created on: Nov 8, 2022
 *      Author: janoko
 */

#include <quectel-ec25.h>
#include <quectel-ec25/core.h>
#include <quectel-ec25/utils.h>
#include <stdlib.h>


static void str2Time(QTEL_Datetime_t*, const char *);

QTEL_Status_t QTEL_CheckAT(QTEL_HandlerTypeDef *qtelPtr)
{
  QTEL_Status_t status = QTEL_ERROR;

  if (AT_Command(&qtelPtr->atCmd, "", 0, 0, 0, 0) == AT_OK) {
    status = QTEL_OK;
    if (qtelPtr->state <= QTEL_STATE_CHECK_AT) {
      qtelPtr->state = QTEL_STATE_CHECK_AT+1;
    }
    QTEL_Echo(qtelPtr, 0);
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_ACTIVE);
  } else {
    qtelPtr->state = QTEL_STATE_CHECK_AT;
    QTEL_UNSET_STATUS(qtelPtr, QTEL_STATUS_ACTIVE);
  }

  return status;
}


QTEL_Status_t QTEL_Echo(QTEL_HandlerTypeDef *qtelPtr, uint8_t onoff)
{
  QTEL_Status_t status = QTEL_ERROR;

  if (AT_Command(&qtelPtr->atCmd, (onoff)? "E1": "E0", 0, 0, 0, 0) == AT_OK) {
    status = QTEL_OK;
  }

  return status;
}


QTEL_Status_t QTEL_CheckSIMCard(QTEL_HandlerTypeDef *qtelPtr)
{
  QTEL_Status_t status = QTEL_ERROR;
  uint8_t respstr[6];
  AT_Data_t respData[1] = {
    AT_Buffer(respstr, sizeof(respstr)),
  };

  memset(respstr, 0, 6);

  AT_Check(&qtelPtr->atCmd, "+CPIN", 1, respData);
  if (strncmp(respData[0].value.string, "READY", 5) == 0) {
    status = QTEL_OK;
  }
  return status;
}


QTEL_Status_t QTEL_CheckNetwork(QTEL_HandlerTypeDef *qtelPtr)
{
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

  if (AT_Check(&qtelPtr->atCmd, "+CREG", 4, respData) != AT_OK) return status;
  qtelPtr->network_status = (uint8_t) respData[1].value.number;

  // check response
  if (qtelPtr->network_status == 1 || qtelPtr->network_status == 5) {
    status = QTEL_OK;
    if (qtelPtr->state <= QTEL_STATE_CHECK_NETWORK) {
      QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
    }
    if (qtelPtr->network_status == 5)
      QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_ROAMING);
  }
  else {
    if (qtelPtr->state > QTEL_STATE_CHECK_NETWORK)
      qtelPtr->state = QTEL_STATE_CHECK_NETWORK;
    QTEL_UNSET_STATUS(qtelPtr, QTEL_STATUS_ROAMING);
  }

  return status;
}


QTEL_Status_t QTEL_ReqisterNetwork(QTEL_HandlerTypeDef *qtelPtr)
{
  QTEL_Status_t status = QTEL_ERROR;
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

  QTEL_Debug("Registering cellular network....");

  // Select operator automatically
  if (AT_Check(&qtelPtr->atCmd, "+COPS", 1, respData) != AT_OK) goto endCmd;
  operator_selection_mode = (uint8_t) respData[0].value.number;

  if (operator_selection_mode != 0) {
    // set
    if (AT_Command(&qtelPtr->atCmd, "+COPS", 1, paramData, 0, 0) != AT_OK) goto endCmd;

    // run
    if (AT_Command(&qtelPtr->atCmd, "+COPS", 0, 0, 0, 0) != AT_OK) goto endCmd;
  }

endCmd:
  return status;
}


QTEL_Status_t QTEL_GetTime(QTEL_HandlerTypeDef *qtelPtr, QTEL_Datetime_t *dt)
{
  uint8_t respstr[24];
  AT_Data_t respData[1] = {
    AT_Buffer(respstr, 24),
  };
  memset(respstr, 0, 24);

  if (AT_Check(&qtelPtr->atCmd, "+CCLK",  1, respData) != AT_OK) return QTEL_ERROR;

  str2Time(dt, (char*)&respstr[0]);

  return QTEL_OK;
}


QTEL_Status_t QTEL_CheckSugnal(QTEL_HandlerTypeDef *qtelPtr)
{
  AT_Data_t respData[2] = {
    AT_Number(0),
    AT_Number(0),
  };

  if (AT_Command(&qtelPtr->atCmd, "+CSQ", 0, 0, 2, respData) != AT_OK) return QTEL_ERROR;
  qtelPtr->signal = respData[1].value.number;

  return QTEL_OK;
}


static void str2Time(QTEL_Datetime_t *dt, const char *str)
{
  uint8_t *dtbytes = (uint8_t*) dt;
  int8_t mult = 1;
  uint8_t len = (uint8_t) sizeof(QTEL_Datetime_t);
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
