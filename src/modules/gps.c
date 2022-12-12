/*
 * gps.c
 *
 *  Created on: Nov 14, 2022
 *      Author: janoko
 */

#include "../include/simcom/gps.h"
#if SIM_EN_FEATURE_GPS

#include "../include/simcom.h"
#include "../events.h"
#include <stdlib.h>
#include <string.h>

#define SIM_GPS_CONFIG_KEY 0xAE

static const SIM_GPS_Config_t defaultConfig = {
  .accuracy = 50,
  .antenaMode = SIM_GPS_ANT_ACTIVE,
  .isAutoDownloadXTRA = 1,
  .outputRate = SIM_GPS_MEARATE_1HZ,
  .reportInterval = 10,
  .NMEA = SIM_GPS_RPT_GPGGA|SIM_GPS_RPT_GPRMC|SIM_GPS_RPT_GPGSV|SIM_GPS_RPT_GPGSA|SIM_GPS_RPT_GPVTG,
  .MOAGPS_Method = SIM_GPS_METHOD_USER_PLANE,
  .agpsServer =  "supl.google.com:7276",
  .isAgpsServerSecure = 0,
};

static SIM_Status_t setConfiguration(SIM_GPS_HandlerTypeDef*);
static SIM_Status_t activate(SIM_GPS_HandlerTypeDef*, uint8_t isActivate);
static void onGetNMEA(void *app, uint8_t *data, uint16_t len);

SIM_Status_t SIM_GPS_Init(SIM_GPS_HandlerTypeDef *hsimGps, void *hsim)
{
  if (((SIM_HandlerTypeDef*)hsim)->key != SIM_KEY)
    return SIM_ERROR;

  hsimGps->hsim = hsim;
  hsimGps->state = SIM_GPS_STATE_NON_ACTIVE;
  hsimGps->stateTick = 0;

  if (hsimGps->isConfigured != SIM_GPS_CONFIG_KEY) {
    hsimGps->isConfigured = SIM_GPS_CONFIG_KEY;
    memcpy(&hsimGps->config, &defaultConfig, sizeof(SIM_GPS_Config_t));
  }

  lwgps_init(&hsimGps->lwgps);

  AT_ReadlineOn(&((SIM_HandlerTypeDef*)hsim)->atCmd, "$", (SIM_HandlerTypeDef*) hsim, onGetNMEA);

  return SIM_OK;
}

void SIM_GPS_SetupConfig(SIM_GPS_HandlerTypeDef *hsimGps, SIM_GPS_Config_t *config)
{
  hsimGps->isConfigured = SIM_GPS_CONFIG_KEY;
  memcpy(&hsimGps->config, config, sizeof(SIM_GPS_Config_t));
}

void SIM_GPS_SetState(SIM_GPS_HandlerTypeDef *hsimGps, uint8_t newState)
{
  hsimGps->state = newState;
  ((SIM_HandlerTypeDef*) hsimGps->hsim)->rtos.eventSet(SIM_RTOS_EVT_GPS_NEW_STATE);
}


void SIM_GPS_OnNewState(SIM_GPS_HandlerTypeDef *hsimGps)
{
  SIM_HandlerTypeDef *hsim = hsimGps->hsim;

  hsimGps->stateTick = hsim->getTick();

  switch (hsimGps->state) {
  case SIM_GPS_STATE_NON_ACTIVE:
    break;

  case SIM_GPS_STATE_SETUP:
    if (activate(hsimGps, 0) == SIM_OK)
      if (setConfiguration(hsimGps) == SIM_OK)
        SIM_GPS_SetState(hsimGps, SIM_GPS_STATE_ACTIVE);
    break;

  case SIM_GPS_STATE_ACTIVE:
    activate(hsimGps, 1);
    break;

  default: break;
  }

  return;
}


static SIM_Status_t setConfiguration(SIM_GPS_HandlerTypeDef *hsimGps)
{
  SIM_Status_t status = SIM_ERROR;
  SIM_HandlerTypeDef *hsim = hsimGps->hsim;
  AT_Data_t paramData[2];

  // set Accuracy
  AT_DataSetNumber(&paramData[0], hsimGps->config.accuracy);
  if (AT_Command(&hsim->atCmd, "+CGPSHOR", 1, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetNumber(&paramData[0], hsimGps->config.outputRate);
  if (AT_Command(&hsim->atCmd, "+CGPSNMEARATE", 1, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetNumber(&paramData[0], hsimGps->config.isAutoDownloadXTRA? 1:0);
  if (AT_Command(&hsim->atCmd, "+CGPSXDAUTO", 1, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetNumber(&paramData[0], hsimGps->config.reportInterval);
  AT_DataSetNumber(&paramData[1], hsimGps->config.NMEA);
  if (AT_Command(&hsim->atCmd, "+CGPSINFOCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetNumber(&paramData[0], hsimGps->config.MOAGPS_Method);
  if (AT_Command(&hsim->atCmd, "+CGPSMD", 1, paramData, 0, 0) != AT_OK) goto endCmd;

  if (hsimGps->config.agpsServer != 0) {
    AT_DataSetString(&paramData[0], hsimGps->config.agpsServer);
    if (AT_Command(&hsim->atCmd, "+CGPSURL", 1, paramData, 0, 0) != AT_OK) goto endCmd;

    AT_DataSetNumber(&paramData[0], hsimGps->config.isAgpsServerSecure? 1:0);
    if (AT_Command(&hsim->atCmd, "+CGPSSSL", 1, paramData, 0, 0) != AT_OK) goto endCmd;
  }

  switch (hsimGps->config.antenaMode) {
  case SIM_GPS_ANT_ACTIVE:
    AT_DataSetNumber(&paramData[0], 3050);
    if (AT_Command(&hsim->atCmd, "+CVAUXV", 1, paramData, 0, 0) != AT_OK) goto endCmd;
    AT_DataSetNumber(&paramData[0], 1);
    if (AT_Command(&hsim->atCmd, "+CVAUXS", 1, paramData, 0, 0) != AT_OK) goto endCmd;
    break;

  case SIM_GPS_ANT_PASSIVE:
  default:
    AT_DataSetNumber(&paramData[0], 0);
    if (AT_Command(&hsim->atCmd, "+CVAUXS", 1, paramData, 0, 0) != AT_OK) goto endCmd;
    break;
  }

  status = SIM_OK;

endCmd:
  return status;
}

static SIM_Status_t activate(SIM_GPS_HandlerTypeDef *hsimGps, uint8_t isActivate)
{
  SIM_HandlerTypeDef *hsim = hsimGps->hsim;
  AT_Data_t paramData[1] = {
      AT_Number(isActivate? 1:0),
  };

  if (AT_Command(&hsim->atCmd, "+CGPS", 1, paramData, 0, 0) != AT_OK) return SIM_ERROR;
  return SIM_OK;
}

static void onGetNMEA(void *app, uint8_t *data, uint16_t len)
{
  SIM_HandlerTypeDef *hsim = (SIM_HandlerTypeDef*)app;
  *(data+len) = 0;

  lwgps_process(&hsim->gps.lwgps, data, len);
}

#endif /* SIM_EN_FEATURE_GPS */
