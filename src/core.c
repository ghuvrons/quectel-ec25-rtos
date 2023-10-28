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
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_ACTIVE);
  } else {
    qtelPtr->state = QTEL_STATE_CHECK_AT;
    QTEL_UNSET_STATUS(qtelPtr, QTEL_STATUS_ACTIVE);
  }

  return status;
}


QTEL_Status_t QTEL_GetFirmwareVersion(QTEL_HandlerTypeDef *qtelPtr)
{
  QTEL_Status_t status = QTEL_ERROR;

  if (AT_Command(&qtelPtr->atCmd, "AT+QGMR", 0, 0, 0, 0) == AT_OK) {
    status = QTEL_OK;
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
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_SIM_READY);
    status = QTEL_OK;
  }

  return status;
}


QTEL_Status_t QTEL_CheckNetwork(QTEL_HandlerTypeDef *qtelPtr)
{
  AT_Status_t status;
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

  status = AT_Check(&qtelPtr->atCmd, "+CREG", 4, respData);
  if (status != AT_OK) return (QTEL_Status_t) status;

  qtelPtr->network_status = (uint8_t) respData[1].value.number;

  // check response
  if (qtelPtr->network_status == 1 || qtelPtr->network_status == 5) {
    if (qtelPtr->state <= QTEL_STATE_CHECK_NETWORK) {
      QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
    }
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED);
  }
  else {
    if (qtelPtr->state > QTEL_STATE_CHECK_NETWORK)
      qtelPtr->state = QTEL_STATE_CHECK_NETWORK;
    QTEL_UNSET_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED);
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_CheckGPRSNetwork(QTEL_HandlerTypeDef *qtelPtr)
{
  AT_Status_t status;
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

  status = AT_Check(&qtelPtr->atCmd, "+CGREG", 4, respData);
  if (status != AT_OK) return (QTEL_Status_t) status;

  qtelPtr->GPRS_network_status = (uint8_t) respData[1].value.number;

  // check response
  if (qtelPtr->GPRS_network_status == 1 || qtelPtr->GPRS_network_status == 5) {
    if (qtelPtr->state <= QTEL_STATE_CHECK_NETWORK) {
      QTEL_SetState(qtelPtr, QTEL_STATE_ACTIVE);
    }
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED);
  }
  else {
    if (qtelPtr->state > QTEL_STATE_CHECK_NETWORK)
      qtelPtr->state = QTEL_STATE_CHECK_NETWORK;
    QTEL_UNSET_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED);
  }

  return QTEL_OK;
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
  AT_Status_t status;
  uint8_t respstr[24];
  AT_Data_t respData[1] = {
      AT_Buffer(respstr, 24),
  };
  memset(respstr, 0, 24);

  status = AT_Check(&qtelPtr->atCmd, "+CCLK", 1, respData);
  if (status != AT_OK) return (QTEL_Status_t) status;

  str2Time(dt, (char*)&respstr[0]);

  return QTEL_OK;
}


QTEL_Status_t QTEL_CheckSugnal(QTEL_HandlerTypeDef *qtelPtr)
{
  AT_Status_t status;
  AT_Data_t respData[2] = {
      AT_Number(0),
      AT_Number(0),
  };

  status = AT_CommandWithTimeout(&qtelPtr->atCmd, "+CSQ", 0, 0, 2, respData, 10000);
  if (status != AT_OK) {
    return (QTEL_Status_t) status;
  }
  if (respData[0].value.number >= 0 && respData[0].value.number < 31) {
    qtelPtr->signal = (uint8_t)(respData[0].value.number * 10 / 3);
  }
  else if (respData[0].value.number >= 100 && respData[0].value.number < 190) {
    qtelPtr->signal = (uint8_t)((respData[0].value.number - 100) * 10 / 9);
  }
  else {
    qtelPtr->signal = 0;
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_GetOperator(QTEL_HandlerTypeDef *qtelPtr)
{
  AT_Status_t status;
  AT_Data_t respData[3] = {
      AT_Number(0),
      AT_Number(0),
      AT_Buffer((uint8_t*)qtelPtr->operator, QTEL_OPERATOR_BUFFER_SIZE),
  };

  status = AT_Check(&qtelPtr->atCmd, "+COPS", 3, respData);
  if (status != AT_OK) {
    return (QTEL_Status_t) status;
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_GetSIMInfo(QTEL_HandlerTypeDef *qtelPtr)
{
  AT_Status_t status;
  AT_Data_t paramData[1] = {
    AT_Number(0),
  };
  AT_Data_t respDataSN[1] = {
      AT_Buffer((uint8_t *)qtelPtr->SIM_SN, QTEL_SIM_SN_BUFFER_SIZE),
  };
  AT_Data_t respDataIMEI[1] = {
      AT_Buffer((uint8_t *)qtelPtr->SIM_IMEI, QTEL_SIM_IMEI_BUFFER_SIZE),
  };

  AT_DataSetNumber(&paramData[0], 0);
  status = AT_Command(&qtelPtr->atCmd, "+CGSN", 1, paramData, 1, respDataSN);
  if (status != AT_OK) {
    return (QTEL_Status_t) status;
  }

  AT_DataSetNumber(&paramData[0], 1);
  status = AT_Command(&qtelPtr->atCmd, "+CGSN", 1, paramData, 1, respDataIMEI);
  if (status != AT_OK) {
    return (QTEL_Status_t) status;
  }

  return QTEL_OK;
}

// AT+QENG="servingcell"

QTEL_Status_t QTEL_CheckQENG(QTEL_HandlerTypeDef *qtelPtr)
{
  AT_Data_t paramData[1] = {
    AT_String("servingcell"),
  };

  return AT_Command(&qtelPtr->atCmd, "+QENG", 1, paramData, 0, 0);
}

static void str2Time(QTEL_Datetime_t *dt, const char *str)
{
  uint8_t *dtbytes = (uint8_t*) dt;
  int8_t mult = 1;
  uint8_t len = (uint8_t) sizeof(QTEL_Datetime_t);
  uint8_t isParsing = 0;

  while (*str && len > 0) {
    if ((*str >= '0' && *str <= '9')) {
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
