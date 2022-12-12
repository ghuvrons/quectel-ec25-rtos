/*
 * gps.c
 *
 *  Created on: Nov 14, 2022
 *      Author: janoko
 */

#include <quectel-ec25/gps.h>
#if QTEL_EN_FEATURE_GPS

#include "../include/quectel-ec25.h"
#include "../events.h"
#include <stdlib.h>
#include <string.h>

#define QTEL_GPS_CONFIG_KEY 0xAE

static const QTEL_GPS_Config_t defaultConfig = {
  .accuracy           = 50,
  .antenaMode         = QTEL_GPS_ANT_ACTIVE,
  .isAutoDownloadXTRA = 1,
  .outputRate         = QTEL_GPS_MEARATE_1HZ,
  .reportInterval     = 10,
  .NMEA               = QTEL_GPS_RPT_GPGGA|QTEL_GPS_RPT_GPRMC|QTEL_GPS_RPT_GPGSV|QTEL_GPS_RPT_GPGSA|QTEL_GPS_RPT_GPVTG,
  .MOAGPS_Method      = QTEL_GPS_METHOD_USER_PLANE,
  .agpsServer         =  "supl.google.com:7276",
  .isAgpsServerSecure = 0,
};

static QTEL_Status_t setConfiguration(QTEL_GPS_HandlerTypeDef*);
static QTEL_Status_t activate(QTEL_GPS_HandlerTypeDef*, uint8_t isActivate);
static void onGetNMEA(void *app, uint8_t *data, uint16_t len);

QTEL_Status_t QTEL_GPS_Init(QTEL_GPS_HandlerTypeDef *qtelGps, void *qtelPtr)
{
  if (((QTEL_HandlerTypeDef*)qtelPtr)->key != QTEL_KEY)
    return QTEL_ERROR;

  qtelGps->qtel = qtelPtr;
  qtelGps->state = QTEL_GPS_STATE_NON_ACTIVE;
  qtelGps->stateTick = 0;

  if (qtelGps->isConfigured != QTEL_GPS_CONFIG_KEY) {
    qtelGps->isConfigured = QTEL_GPS_CONFIG_KEY;
    memcpy(&qtelGps->config, &defaultConfig, sizeof(QTEL_GPS_Config_t));
  }

  lwgps_init(&qtelGps->lwgps);

  AT_ReadlineOn(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "$", (QTEL_HandlerTypeDef*) qtelPtr, onGetNMEA);

  return QTEL_OK;
}

void QTEL_GPS_SetupConfig(QTEL_GPS_HandlerTypeDef *qtelGps, QTEL_GPS_Config_t *config)
{
  qtelGps->isConfigured = QTEL_GPS_CONFIG_KEY;
  memcpy(&qtelGps->config, config, sizeof(QTEL_GPS_Config_t));
}

void QTEL_GPS_SetState(QTEL_GPS_HandlerTypeDef *qtelGps, uint8_t newState)
{
  qtelGps->state = newState;
  ((QTEL_HandlerTypeDef*) qtelGps->qtel)->rtos.eventSet(QTEL_RTOS_EVT_GPS_NEW_STATE);
}


void QTEL_GPS_OnNewState(QTEL_GPS_HandlerTypeDef *qtelGps)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;

  qtelGps->stateTick = qtelPtr->getTick();

  switch (qtelGps->state) {
  case QTEL_GPS_STATE_NON_ACTIVE:
    break;

  case QTEL_GPS_STATE_SETUP:
    if (activate(qtelGps, 0) == QTEL_OK)
      if (setConfiguration(qtelGps) == QTEL_OK)
        QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_ACTIVE);
    break;

  case QTEL_GPS_STATE_ACTIVE:
    activate(qtelGps, 1);
    break;

  default: break;
  }

  return;
}


static QTEL_Status_t setConfiguration(QTEL_GPS_HandlerTypeDef *qtelGps)
{
  QTEL_Status_t status = QTEL_ERROR;
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;
  AT_Data_t paramData[2];

  // set Accuracy
  AT_DataSetNumber(&paramData[0], qtelGps->config.accuracy);
  if (AT_Command(&qtelPtr->atCmd, "+CGPSHOR", 1, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetNumber(&paramData[0], qtelGps->config.outputRate);
  if (AT_Command(&qtelPtr->atCmd, "+CGPSNMEARATE", 1, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetNumber(&paramData[0], qtelGps->config.isAutoDownloadXTRA? 1:0);
  if (AT_Command(&qtelPtr->atCmd, "+CGPSXDAUTO", 1, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetNumber(&paramData[0], qtelGps->config.reportInterval);
  AT_DataSetNumber(&paramData[1], qtelGps->config.NMEA);
  if (AT_Command(&qtelPtr->atCmd, "+CGPSINFOCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetNumber(&paramData[0], qtelGps->config.MOAGPS_Method);
  if (AT_Command(&qtelPtr->atCmd, "+CGPSMD", 1, paramData, 0, 0) != AT_OK) goto endCmd;

  if (qtelGps->config.agpsServer != 0) {
    AT_DataSetString(&paramData[0], qtelGps->config.agpsServer);
    if (AT_Command(&qtelPtr->atCmd, "+CGPSURL", 1, paramData, 0, 0) != AT_OK) goto endCmd;

    AT_DataSetNumber(&paramData[0], qtelGps->config.isAgpsServerSecure? 1:0);
    if (AT_Command(&qtelPtr->atCmd, "+CGPSSSL", 1, paramData, 0, 0) != AT_OK) goto endCmd;
  }

  switch (qtelGps->config.antenaMode) {
  case QTEL_GPS_ANT_ACTIVE:
    AT_DataSetNumber(&paramData[0], 3050);
    if (AT_Command(&qtelPtr->atCmd, "+CVAUXV", 1, paramData, 0, 0) != AT_OK) goto endCmd;
    AT_DataSetNumber(&paramData[0], 1);
    if (AT_Command(&qtelPtr->atCmd, "+CVAUXS", 1, paramData, 0, 0) != AT_OK) goto endCmd;
    break;

  case QTEL_GPS_ANT_PASSIVE:
  default:
    AT_DataSetNumber(&paramData[0], 0);
    if (AT_Command(&qtelPtr->atCmd, "+CVAUXS", 1, paramData, 0, 0) != AT_OK) goto endCmd;
    break;
  }

  status = QTEL_OK;

endCmd:
  return status;
}

static QTEL_Status_t activate(QTEL_GPS_HandlerTypeDef *qtelGps, uint8_t isActivate)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;
  AT_Data_t paramData[1] = {
      AT_Number(isActivate? 1:0),
  };

  if (AT_Command(&qtelPtr->atCmd, "+CGPS", 1, paramData, 0, 0) != AT_OK) return QTEL_ERROR;
  return QTEL_OK;
}

static void onGetNMEA(void *app, uint8_t *data, uint16_t len)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;
  *(data+len) = 0;

  lwgps_process(&qtelPtr->gps.lwgps, data, len);
}

#endif /* QTEL_EN_FEATURE_GPS */
