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
  } else {
    qtelPtr->state = QTEL_STATE_CHECK_AT;
  }

  return status;
}


QTEL_Status_t QTEL_PowerDown(QTEL_HandlerTypeDef *qtelPtr)
{
  if (AT_Command(&qtelPtr->atCmd, "+QPOWD", 0, 0, 0, 0) == AT_OK) {
    qtelPtr->state = QTEL_STATE_POWERING_DOWN;
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_GetFirmwareVersion(QTEL_HandlerTypeDef *qtelPtr)
{
  QTEL_Status_t status = QTEL_ERROR;

  if (AT_Command(&qtelPtr->atCmd, "+QGMR", 0, 0, 0, 0) == AT_OK) {
    status = QTEL_OK;
  }

  return status;
}


QTEL_Status_t QTEL_GetICCID(QTEL_HandlerTypeDef *qtelPtr)
{
  QTEL_Status_t status = QTEL_ERROR;
  AT_Data_t resp[1] = {
      AT_Buffer((uint8_t *)qtelPtr->iccid, QTEL_ICCID_BUFFER_SIZE),
  };

  if (AT_Command(&qtelPtr->atCmd, "AT+QCCID", 0, 0, 1, resp) == AT_OK) {
    status = QTEL_OK;
  }

  return status;
}


QTEL_Status_t QTEL_GetError(QTEL_HandlerTypeDef *qtelPtr)
{
  QTEL_Status_t status = QTEL_ERROR;

  if (AT_Command(&qtelPtr->atCmd, "AT+QIGETERROR", 0, 0, 0, 0) == AT_OK) {
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
      AT_Number(-1),
      AT_Hex(lac),
      AT_Hex(ci),
  };

  memset(lac, 0, 2);
  memset(ci, 0, 2);

  status = AT_Check(&qtelPtr->atCmd, "+CREG", 4, respData);
  if (status != AT_OK) return (QTEL_Status_t) status;

  if (respData[1].value.number == -1) // from urc
    qtelPtr->network_status = (uint8_t) respData[0].value.number;
  else
    qtelPtr->network_status = (uint8_t) respData[1].value.number;

  // check response
  if (qtelPtr->network_status == 1 || qtelPtr->network_status == 5) {
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_NET_REGISTERED);
  }
  else {
    if (qtelPtr->state > QTEL_STATE_CHECK_NETWORK) {
      qtelPtr->state = QTEL_STATE_CHECK_NETWORK;
      qtelPtr->tick.checkNetwork = qtelPtr->getTick();
      qtelPtr->tick.changedState = qtelPtr->getTick();
    }
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
      AT_Number(-1),
      AT_Hex(lac),
      AT_Hex(ci),
  };

  memset(lac, 0, 2);
  memset(ci, 0, 2);

  status = AT_Check(&qtelPtr->atCmd, "+CGREG", 4, respData);
  if (status != AT_OK) return (QTEL_Status_t) status;

  if (respData[1].value.number == -1)
    qtelPtr->GPRS_network_status = (uint8_t) respData[0].value.number;
  else
    qtelPtr->GPRS_network_status = (uint8_t) respData[1].value.number;

  // check response
  if (qtelPtr->GPRS_network_status == 1 || qtelPtr->GPRS_network_status == 5) {
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED);

#if QTEL_EN_FEATURE_NET
    if (qtelPtr->state == QTEL_STATE_ACTIVE) {
      if (qtelPtr->net.state == QTEL_NET_STATE_ACTIVATING_PENDING)
        QTEL_NET_SetState(&qtelPtr->net, QTEL_NET_STATE_ACTIVATING);
#if QTEL_EN_FEATURE_SOCKET
      else if (qtelPtr->net.state == QTEL_NET_STATE_ACTIVE
               && qtelPtr->socketManager.state == QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING)
        QTEL_SockManager_SetState(&qtelPtr->socketManager, QTEL_SOCKH_STATE_PDP_ACTIVATING);
#endif /* QTEL_EN_FEATURE_SOCKET */
    }
#endif /* QTEL_EN_FEATURE_NET */
  }

  else {
    QTEL_UNSET_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED);
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_CheckLTENetwork(QTEL_HandlerTypeDef *qtelPtr)
{
  AT_Status_t status;
  uint8_t lac[2]; // location area code
  uint8_t ci[2];  // Cell Identify

  AT_Data_t respData[4] = {
      AT_Number(0),
      AT_Number(-1),
      AT_Hex(lac),
      AT_Hex(ci),
  };

  memset(lac, 0, 2);
  memset(ci, 0, 2);

  status = AT_Check(&qtelPtr->atCmd, "+CEREG", 4, respData);
  if (status != AT_OK) return (QTEL_Status_t) status;
  if (respData[1].value.number == -1)
    qtelPtr->LTE_network_status = (uint8_t) respData[0].value.number;
  else
    qtelPtr->LTE_network_status = (uint8_t) respData[1].value.number;

  // check response
  if (qtelPtr->LTE_network_status == 1 || qtelPtr->LTE_network_status == 5) {
    QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_LTE_REGISTERED);

#if QTEL_EN_FEATURE_NET
    if (qtelPtr->state == QTEL_STATE_ACTIVE) {
      if (qtelPtr->net.state == QTEL_NET_STATE_ACTIVATING_PENDING)
        QTEL_NET_SetState(&qtelPtr->net, QTEL_NET_STATE_ACTIVATING);
#if QTEL_EN_FEATURE_SOCKET
      else if (qtelPtr->net.state == QTEL_NET_STATE_ACTIVE
               && qtelPtr->socketManager.state == QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING)
        QTEL_SockManager_SetState(&qtelPtr->socketManager, QTEL_SOCKH_STATE_PDP_ACTIVATING);
#endif /* QTEL_EN_FEATURE_SOCKET */
    }
#endif /* QTEL_EN_FEATURE_NET */
  }

  else {
    QTEL_UNSET_STATUS(qtelPtr, QTEL_STATUS_LTE_REGISTERED);
  }

  return QTEL_OK;
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
  if (respData[0].value.number >= 0 && respData[0].value.number <= 31) {
    qtelPtr->signal = (uint8_t)(respData[0].value.number * 100 / 31);
  }
  else if (respData[0].value.number >= 100 && respData[0].value.number <= 191) {
    qtelPtr->signal = (uint8_t)((respData[0].value.number - 100) * 100 / 91);
  }
  else if (respData[0].value.number == 99 || respData[0].value.number == 199) {
    qtelPtr->signal = 0;
  }

  return QTEL_OK;
}

/**
 *
 * @param qtelPtr
 * @param operator
 * @return status
 */
QTEL_Status_t QTEL_SetOperator(QTEL_HandlerTypeDef *qtelPtr, const char *operator)
{
  AT_Status_t status;
  int mode = 4;
  AT_Data_t paramData[3] = {
      AT_Number(0),
      AT_Number(0),
      AT_String(operator),
  };
  AT_Data_t respData[3];

  uint32_t tick = qtelPtr->getTick();
  while (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_RESP_BUF_LOCK)) {
    if ((qtelPtr->getTick() - tick) > RESP_BUF_LOCK_TIMEOUT) return QTEL_TIMEOUT;
    qtelPtr->delay(1);
  }
  QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_RESP_BUF_LOCK);

  AT_DataSetNumber(&respData[0], 0);
  AT_DataSetNumber(&respData[1], 0);
  AT_DataSetBuffer(&respData[2], qtelPtr->respBuffer, QTEL_RESP_BUFFER_SIZE);

  status = AT_Check(&qtelPtr->atCmd, "+COPS", 3, respData);
  if (status != AT_OK) goto endFunc;

  if (operator == 0 || operator[0] == 0) {
    mode = 0;
  }

  if (mode == respData[0].value.number) {
    if (mode == 4) {
      // check format and operator
      if (respData[1].value.number == 0 && strncmp(respData[0].value.string, operator, QTEL_RESP_BUFFER_SIZE) == 0) {
         goto endFunc;
      }
    }
    else goto endFunc;
  }
  
  paramData[0].value.number = mode;
  status = AT_CommandWithTimeout(&qtelPtr->atCmd, "+COPS", 3, paramData, 0, 0, 190000);
  if (status != AT_OK) goto endFunc;

endFunc:
  QTEL_UNSET_STATUS(qtelPtr, QTEL_STATUS_RESP_BUF_LOCK);
  return (QTEL_Status_t) status;
}


QTEL_Status_t QTEL_GetAvailableOperator(QTEL_HandlerTypeDef *qtelPtr)
{
  AT_Status_t status;
  AT_Data_t respData[7][5];

  if (QTEL_RESP_BUFFER_SIZE < 7*3*16) return QTEL_ERROR;

  uint32_t tick = qtelPtr->getTick();
  while (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_RESP_BUF_LOCK)) {
    if ((qtelPtr->getTick() - tick) > RESP_BUF_LOCK_TIMEOUT) return QTEL_TIMEOUT;
    qtelPtr->delay(1);
  }
  QTEL_SET_STATUS(qtelPtr, QTEL_STATUS_RESP_BUF_LOCK);
  memset(&qtelPtr->respBuffer[0], 0, 7*3*16);

  for (uint8_t i = 0; i < 7; i++) {
    AT_DataSetNumber(&respData[i][0], 0);
    AT_DataSetBuffer(&respData[i][1], &qtelPtr->respBuffer[((i * 3) + 0)*16], 16);
    AT_DataSetBuffer(&respData[i][2], &qtelPtr->respBuffer[((i * 3) + 1)*16], 16);
    AT_DataSetBuffer(&respData[i][3], &qtelPtr->respBuffer[((i * 3) + 2)*16], 16);
    AT_DataSetNumber(&respData[i][4], 0);
  }

  status = AT_TestWithTimeout(&qtelPtr->atCmd, "+COPS",
                              7, 5, &respData[0][0], 190000);

  QTEL_UNSET_STATUS(qtelPtr, QTEL_STATUS_RESP_BUF_LOCK);
  return (QTEL_Status_t) status;
}


QTEL_Status_t QTEL_GetOperator(QTEL_HandlerTypeDef *qtelPtr)
{
  AT_Status_t status;
  AT_Data_t respData[3] = {
      AT_Number(0),
      AT_Number(0),
      AT_Buffer((uint8_t*)qtelPtr->registeredOperator, QTEL_OPERATOR_BUFFER_SIZE),
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
