/*
 * gps.c
 *
 *  Created on: Nov 14, 2022
 *      Author: janoko
 */

#include <quectel-ec25/gps.h>

#if QTEL_EN_FEATURE_GPS
#include "../events.h"
#include <quectel-ec25.h>
#include <quectel-ec25/core.h>
#include <quectel-ec25/utils.h>
#include <stdlib.h>
#include <string.h>

#define QTEL_GPS_CONFIG_KEY   0xAE0114F3
#define QTEL_ONEXTRA_TMP_FILE "RAM:xtra2.bin"

static const QTEL_GPS_Config_t defaultConfig = {
  .accuracy           = 50,
  .outputRate         = QTEL_GPS_MEARATE_1HZ,
  .NMEA               = QTEL_GPS_RPT_GPGGA  |
                        QTEL_GPS_RPT_GPRMC  |
                        QTEL_GPS_RPT_GPGSV  |
                        QTEL_GPS_RPT_GPGSA  |
                        QTEL_GPS_RPT_GPVTG,
  .planeMode          = QTEL_GPS_USER_PLANE,
  .AGPS_Mode          = QTEL_AGPS_MODE_STANDALONE               |
                        QTEL_AGPS_MODE_UP_MS_BASED              |
                        QTEL_AGPS_MODE_UP_MS_ASSISTED           |
                        QTEL_AGPS_MODE_CP_MS_BASED_2G           |
                        QTEL_AGPS_MODE_CP_MS_ASSISTED_2G        |
                        QTEL_AGPS_MODE_CP_MS_BASED_3G           |
                        QTEL_AGPS_MODE_CP_MS_ASSISTED_3G        |
                        QTEL_AGPS_MODE_UP_MS_BASED_4G           |
                        QTEL_AGPS_MODE_UP_MS_ASSISTED_4G        |
                        QTEL_AGPS_MODE_CP_MS_BASED_4G           |
                        QTEL_AGPS_MODE_CP_MS_ASSISTED_4G        |
                        QTEL_AGPS_MODE_AGLONASS_UP_MSB_3G       |
                        QTEL_AGPS_MODE_AGLONASS_UP_MSA_3G       |
                        QTEL_AGPS_MODE_AGLONASS_CP_MSB_3G       |
                        QTEL_AGPS_MODE_AGLONASS_CP_MSA_3G       |
                        QTEL_AGPS_MODE_AGLONASS_UP_MSB_4G       |
                        QTEL_AGPS_MODE_AGLONASS_UP_MSA_4G       |
                        QTEL_AGPS_MODE_AGLONASS_CP_MSB_4G       |
                        QTEL_AGPS_MODE_AGLONASS_CP_MSA_4G,
  .mode               = QTEL_GPS_STANDALONE,
  .AGPS_Protocols     = QTEL_AGPS_PTC_USER_PLANE_LPP |
                        QTEL_AGPS_PTC_CONTROL_PLANE_LPP,
  .AGLONASS_Protocols = QTEL_AGLONASS_PTC_CONTROL_PLANE_RRLP  |
                        QTEL_AGLONASS_PTC_CONTROL_PLANE_RRC   |
                        QTEL_AGLONASS_PTC_CONTROL_PLANE_LPP   |
                        QTEL_AGLONASS_PTC_USER_PLANE_RRLP     |
                        QTEL_AGLONASS_PTC_USER_PLANE_LPP,

  .supl         =  {
      .version  = QTEL_GPS_SUPL_V2,
      .server   = "supl.google.com:7276",
  },
  .oneXTRA = {
      .dataURL = "http://xtrapath1.izatcloud.net/xtra3grc.bin",
  },
};


static QTEL_Status_t setConfiguration(QTEL_GPS_HandlerTypeDef*);
static QTEL_Status_t stopGPS(QTEL_GPS_HandlerTypeDef*);
static QTEL_Status_t startGPS(QTEL_GPS_HandlerTypeDef*, QTEL_GPS_Mode_t);
#if QTEL_EN_FEATURE_GPS_ONEXTRA
static QTEL_Status_t configureOneXTRA(QTEL_GPS_HandlerTypeDef*);
#endif
static QTEL_Status_t acquirePosition(QTEL_GPS_HandlerTypeDef*);
static void parseTimeStr(QTEL_Datetime_t *dst, const char *src);

#if QTEL_DEBUG_GPSNMEA
static QTEL_Status_t getNMEA(QTEL_GPS_HandlerTypeDef*, QTEL_GPS_NMEAFormatType_t);
#endif /* QTEL_DEBUG_GPSNMEA */

QTEL_Status_t QTEL_GPS_Init(QTEL_GPS_HandlerTypeDef *qtelGps, void *qtelPtr)
{
  if (((QTEL_HandlerTypeDef*)qtelPtr)->key != QTEL_KEY)
    return QTEL_ERROR;

  qtelGps->qtel = qtelPtr;
  qtelGps->state = QTEL_GPS_STATE_NON_ACTIVE;
  qtelGps->stateTick = 0;
  qtelGps->restartTick = 0;
  qtelGps->restartLimitTick = 0;

  if (qtelGps->config.key != QTEL_GPS_CONFIG_KEY) {
    QTEL_GPS_SetupConfig(qtelGps, &defaultConfig);
  }

#if QTEL_DEBUG_GPSNMEA
  memset(&qtelGps->nmea.GGA, 0, 128);
  memset(&qtelGps->nmea.RMC, 0, 128);
  memset(&qtelGps->nmea.GSV, 0, 128);
  memset(&qtelGps->nmea.GSA, 0, 128);
  memset(&qtelGps->nmea.VTG, 0, 128);
  memset(&qtelGps->nmea.GNS, 0, 128);
#endif /* QTEL_DEBUG_GPSNMEA */

  return QTEL_OK;
}

void QTEL_GPS_SetupConfig(QTEL_GPS_HandlerTypeDef *qtelGps, const QTEL_GPS_Config_t *config)
{
  memcpy(&qtelGps->config, config, sizeof(QTEL_GPS_Config_t));
  qtelGps->config.key = QTEL_GPS_CONFIG_KEY;
}

void QTEL_GPS_OnPoweredDown(QTEL_GPS_HandlerTypeDef *qtelGps)
{
  QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_NON_ACTIVE);
}

void QTEL_GPS_SetState(QTEL_GPS_HandlerTypeDef *qtelGps, QTEL_GPS_State_t newState)
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
    qtelGps->isConfigured = 0;
    qtelGps->isOneExtraActive = 0;
    break;

  case QTEL_GPS_STATE_STARTING:
    if (qtelPtr->state < QTEL_STATE_ACTIVE) {
      QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_NON_ACTIVE);
      break;
    }

    if (setConfiguration(qtelGps) != QTEL_OK) {
      QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_NON_ACTIVE);
      break;
    }

    if (QTEL_IS_STATUS(&qtelPtr->ntp, QTEL_NTP_WAS_SYNCED)) {
      if (startGPS(qtelGps, qtelGps->config.mode) != QTEL_OK) {
        break;
      }

      QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_FIXING);
    }
    break;

  case QTEL_GPS_STATE_FIXING:
    if (qtelGps->callback.onFixing)
      qtelGps->callback.onFixing();
    qtelGps->getLocTick = qtelGps->stateTick;
    qtelGps->restartTick = 0;
    qtelGps->tryRestarting = 0;
    break;

  case QTEL_GPS_STATE_FIXED:
    if (qtelGps->callback.onFixed)
      qtelGps->callback.onFixed();
    QTEL_Debug("[GPS] fixed");
    qtelGps->acquireErrorCounter = 0;
    qtelGps->getLocTick = qtelGps->stateTick;
    break;

  default: break;
  }

  return;
}

void QTEL_GPS_Loop(QTEL_GPS_HandlerTypeDef *qtelGps)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;

  switch (qtelGps->state) {
  case QTEL_GPS_STATE_NON_ACTIVE:
    if (qtelPtr->state >= QTEL_STATE_ACTIVE && qtelGps->isEnable) {
      if (QTEL_IsTimeout(qtelPtr, qtelGps->stateTick, 2000)) {
        QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_STARTING);
      }
    }
    break;

  case QTEL_GPS_STATE_STARTING:
    if (QTEL_IsTimeout(qtelPtr, qtelGps->stateTick, 5000)) {
      if (qtelPtr->state <= QTEL_STATE_STARTING) {
        QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_NON_ACTIVE);
        break;
      }
      QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_STARTING);
    }
    break;

  case QTEL_GPS_STATE_FIXING:
    if (qtelPtr->state <= QTEL_STATE_STARTING) {
      QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_NON_ACTIVE);
      break;
    }
    if (QTEL_IsTimeout(qtelPtr, qtelGps->getLocTick, 2000)) {
      if (acquirePosition(qtelGps) == QTEL_OK) {
        QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_FIXED);
        break;
      }
    }

    if (QTEL_IsTimeout(qtelPtr, qtelGps->stateTick, 180000 /* 3 minutes */)) {
      if (qtelGps->tryRestarting == 0)
        qtelGps->tryRestarting = 1;
    }

    if (qtelGps->tryRestarting) {
      if (qtelGps->restartTick == 0 || QTEL_IsTimeout(qtelPtr, qtelGps->restartTick, 180000)) {
        // limit restart when not standalone for saving data usage
        if (qtelGps->config.mode != QTEL_GPS_STANDALONE) {
          if (qtelGps->restartLimitTick == 0 || QTEL_IsTimeout(qtelPtr, qtelGps->restartLimitTick, 1800000)) {
            qtelGps->restartCounter = 0;
            qtelGps->restartLimitTick = qtelPtr->getTick();
          }
          else if (qtelGps->restartCounter < 5)
            qtelGps->restartCounter += 1;
        }

        if (qtelGps->config.mode == QTEL_GPS_STANDALONE || qtelGps->restartCounter < 5) {
          if (qtelGps->callback.onRestarting)
            qtelGps->callback.onRestarting();
          if (startGPS(qtelGps, qtelGps->config.mode) != QTEL_OK) {
            QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_STARTING);
            break;
          }
        }

        qtelGps->restartTick = qtelPtr->getTick();
      }
    }
    break;


  case QTEL_GPS_STATE_FIXED:
    if (qtelPtr->state <= QTEL_STATE_STARTING) {
      QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_NON_ACTIVE);
      break;
    }
    if (QTEL_IsTimeout(qtelPtr, qtelGps->getLocTick, 5000)) {
      if (acquirePosition(qtelGps) != QTEL_OK) {
        QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_FIXING);
        break;
      }
    }
    break;

  default: break;
  }

#if QTEL_DEBUG_GPSNMEA
  if (qtelGps->state == QTEL_GPS_STATE_FIXING || qtelGps->state == QTEL_GPS_STATE_FIXED) {
    getNMEA(qtelGps, QTEL_GPS_GGA);
    getNMEA(qtelGps, QTEL_GPS_RMC);
    getNMEA(qtelGps, QTEL_GPS_GSV);
    getNMEA(qtelGps, QTEL_GPS_GSA);
    getNMEA(qtelGps, QTEL_GPS_VTG);
    getNMEA(qtelGps, QTEL_GPS_GNS);
  }
#endif

  return;
}


void QTEL_GPS_Activate(QTEL_GPS_HandlerTypeDef *qtelGps)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;
  qtelGps->isEnable = 1;

  if (qtelPtr->state >= QTEL_STATE_ACTIVE) {
    QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_STARTING);
  }
}

const QTEL_GPS_Config_t* QTEL_GPS_GetDefaultConfig(void)
{
  return &defaultConfig;
}

static QTEL_Status_t setConfiguration(QTEL_GPS_HandlerTypeDef *qtelGps)
{
  QTEL_Status_t status = QTEL_ERROR;
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;
  AT_Data_t paramData[3];
  uint8_t respstr[32];
  AT_Data_t respData[3] = {
      AT_Buffer(&respstr[0], 32),
      AT_Number(0),
      AT_Number(0),
  };

  if (qtelGps->isConfigured) return QTEL_OK;

  AT_DataSetString(&paramData[0], "gnssconfig");
  AT_DataSetNumber(&paramData[1], 1);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 1, paramData, 2, respData) != AT_OK ||
      respData[1].value.number != 1)
  {
    if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;
  }

  AT_DataSetString(&paramData[0], "suplver");
  AT_DataSetNumber(&paramData[1], qtelGps->config.supl.version);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 1, paramData, 2, respData) != AT_OK ||
      respData[1].value.number != qtelGps->config.supl.version)
  {
    if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;
  }

  AT_DataSetString(&paramData[0], "plane");
  AT_DataSetNumber(&paramData[1], qtelGps->config.planeMode);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 1, paramData, 2, respData) != AT_OK ||
      respData[1].value.number != qtelGps->config.planeMode)
  {
    if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;
  }

  AT_DataSetString(&paramData[0], "agpsposmode");
  AT_DataSetNumber(&paramData[1], qtelGps->config.AGPS_Mode);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 1, paramData, 2, respData) != AT_OK ||
      respData[1].value.number != qtelGps->config.AGPS_Mode)
  {
    if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;
  }

  AT_DataSetString(&paramData[0], "agnssprotocol");
  AT_DataSetNumber(&paramData[1], qtelGps->config.AGPS_Protocols);
  AT_DataSetNumber(&paramData[2], qtelGps->config.AGLONASS_Protocols);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 1, paramData, 3, respData) != AT_OK ||
      respData[1].value.number != qtelGps->config.AGPS_Protocols ||
      respData[2].value.number != qtelGps->config.AGLONASS_Protocols)
  {
    if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 3, paramData, 0, 0) != AT_OK) goto endCmd;
  }

  AT_DataSetString(&paramData[0], "fixfreq");
  AT_DataSetNumber(&paramData[1], qtelGps->config.outputRate);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 1, paramData, 2, respData) != AT_OK ||
      respData[1].value.number != qtelGps->config.outputRate)
  {
    if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;
  }

  if (qtelGps->config.supl.server != 0) {
    AT_DataSetString(&paramData[0], qtelGps->config.supl.server);
    if (AT_Check(&qtelPtr->atCmd, "+QGPSSUPLURL", 1, respData) != AT_OK ||
        !(respData[0].type == AT_STRING &&
          strncmp(respData[0].value.string, qtelGps->config.supl.server, 32) == 0
        ))
    {
      if (AT_Command(&qtelPtr->atCmd, "+QGPSSUPLURL", 1, paramData, 0, 0) != AT_OK) goto endCmd;
    }
  }

  qtelGps->isConfigured = 1;
  status = QTEL_OK;

endCmd:
  return status;
}


static QTEL_Status_t stopGPS(QTEL_GPS_HandlerTypeDef *qtelGps)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;

  if (AT_Command(&qtelPtr->atCmd, "+QGPSEND", 0, 0, 0, 0) != AT_OK)
    return QTEL_ERROR;

  return QTEL_OK;
}


static QTEL_Status_t startGPS(QTEL_GPS_HandlerTypeDef *qtelGps,
                              QTEL_GPS_Mode_t mode)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;
  AT_Data_t paramData[1] = {
      AT_Number(mode),
  };
  AT_Data_t respData[1] = {
      AT_Number(0),
  };

  if (AT_Check(&qtelPtr->atCmd, "+QGPS", 1, respData) == AT_OK) {
    if (respData[0].type == AT_NUMBER && respData[0].value.number == 0) {
      QTEL_Debug("[GPS] start with mode %d", (int) mode);
      goto activateGPS;
    } else {
      QTEL_Debug("[GPS] switch to mode %d", (int) mode);
    }
  }

  stopGPS(qtelGps);

activateGPS:
#if QTEL_EN_FEATURE_GPS_ONEXTRA
  if (mode == QTEL_GPS_STANDALONE) {
    if (QTEL_IS_STATUS(&qtelPtr->ntp, QTEL_NTP_WAS_SYNCED)) {
      configureOneXTRA(qtelGps);
    }
  }
  else {
    AT_Command(&qtelPtr->atCmd, "+QGPSXTRA=0", 0, 0, 0, 0);
  }
#endif
  if (AT_Command(&qtelPtr->atCmd, "+QGPS", 1, paramData, 0, 0) != AT_OK)
    return QTEL_ERROR;

  qtelGps->getLocTick = qtelPtr->getTick();
  return QTEL_OK;
}


#if QTEL_EN_FEATURE_GPS_ONEXTRA
static QTEL_Status_t configureOneXTRA(QTEL_GPS_HandlerTypeDef *qtelGps)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;
  QTEL_HTTP_Response_t resp;
  QTEL_Datetime_t currenttime;
  QTEL_Datetime_t xtratime;
  AT_Data_t paramData[5];
  uint8_t xtratimeStr[25];
  AT_Data_t respData[2] = {
      AT_Number(0),
      AT_Buffer(xtratimeStr, sizeof(xtratimeStr)),
  };

  qtelGps->isOneExtraActive = 0;

  if (qtelGps->config.oneXTRA.dataURL != 0) {
    if (AT_Check(&qtelPtr->atCmd, "+QGPSXTRA", 1, respData) != AT_OK)
      return QTEL_ERROR;

    if (respData[0].type != AT_NUMBER || respData[0].value.number != 1) {
      if (AT_Command(&qtelPtr->atCmd, "+QGPSXTRA=1", 0, 0, 0, 0) != AT_OK)
        return QTEL_ERROR;
    }

    if (AT_Check(&qtelPtr->atCmd, "+QGPSXTRADATA", 2, respData) != AT_OK)
      goto handleError;

    if (respData[0].type == AT_NUMBER &&
        respData[1].type == AT_STRING &&
        respData[1].value.string != 0)
    {
      memset(&xtratime, 0, sizeof(QTEL_Datetime_t));
      parseTimeStr(&xtratime, respData[1].value.string);
      QTEL_Datetime_AddSeconds(&xtratime, (respData[0].value.number * 60));

      QTEL_GetTime(qtelPtr, &currenttime);
      QTEL_Datetime_SetToUTC(&currenttime);

      // if currenttime > (xtratime (expiredtime) - 1 day)
      if (QTEL_Datetime_Diff(&currenttime, &xtratime) <= 1440)
      {
        qtelGps->isOneExtraActive = 1;
        return QTEL_OK;
      }
    }
    else goto handleError;

    if (!(QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED)
        || QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_LTE_REGISTERED)))
    {
      goto handleError;
    }

    QTEL_FILE_RemoveFile(&qtelPtr->file, QTEL_ONEXTRA_TMP_FILE);
    if (QTEL_HTTP_DownloadAndSave(&qtelPtr->http,
                                  qtelGps->config.oneXTRA.dataURL,
                                  QTEL_ONEXTRA_TMP_FILE,
                                  &resp,
                                  60000) != QTEL_OK)
    {
      goto handleError;
    }

    QTEL_GetTime(qtelPtr, &currenttime);
    snprintf((char*)xtratimeStr, 25, "%02d/%02d/%02d,%02d:%02d:%02d",
             ((int)currenttime.year) + 2000,
             (int) currenttime.month,
             (int) currenttime.day,
             (int) currenttime.hour,
             (int) currenttime.minute,
             (int) currenttime.second
             );

    AT_DataSetNumber(&paramData[0], 0);
    AT_DataSetString(&paramData[1], (char*)xtratimeStr);
    AT_DataSetNumber(&paramData[2], 1);
    AT_DataSetNumber(&paramData[3], 1);
    AT_DataSetNumber(&paramData[4], 3500);
    if (AT_Command(&qtelPtr->atCmd, "+QGPSXTRATIME", 5, paramData, 0, 0) != AT_OK)
      goto handleError;

    AT_DataSetString(&paramData[0], QTEL_ONEXTRA_TMP_FILE);
    if (AT_Command(&qtelPtr->atCmd, "+QGPSXTRADATA", 1, paramData, 0, 0) != AT_OK)
      goto handleError;

    qtelGps->isOneExtraActive = 1;
  }
  else {
    // disable oneXtra
    if (AT_Check(&qtelPtr->atCmd, "+QGPSXTRA", 1, respData) != AT_OK)
      return QTEL_ERROR;

    if (respData[0].type != AT_NUMBER || respData[0].value.number != 0) {
      AT_DataSetNumber(&paramData[0], 0);
      if (AT_Command(&qtelPtr->atCmd, "+QGPSXTRA", 1, paramData, 0, 0) != AT_OK)
        return QTEL_ERROR;
    }
  }

  return QTEL_OK;

handleError:
  // disable oneXtra
  AT_Command(&qtelPtr->atCmd, "+QGPSXTRA=0", 0, 0, 0, 0);
  return QTEL_ERROR;
}
#endif /* QTEL_EN_FEATURE_GPS_ONEXTRA */


static QTEL_Status_t acquirePosition(QTEL_GPS_HandlerTypeDef *qtelGps)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;

  struct {
    uint8_t utc[11];
    uint8_t date[7];
  } tmpBuffer;

  AT_Data_t paramData[1];
  AT_Data_t respData[11] = {
      AT_Buffer(tmpBuffer.utc, 11), // UTC
      AT_Float(0),                  // Latitude
      AT_Float(0),                  // Longitude
      AT_Float(0),                  // HDOP : float one digit after point
      AT_Float(0),                  // Altitude
      AT_Number(0),                 // fix : 2 (2D), 3 (3D)
      AT_Float(0),                  // COG : Curve over ground
      AT_Float(0),                  // spkm : speed over ground (kmph)
      AT_Float(0),                  // spkn : speed over ground (spkn)
      AT_Buffer(tmpBuffer.date, 7), // Date : yymmdd
      AT_Number(0)                  // number of satellites
  };

  AT_DataSetNumber(&paramData[0], 2);

  qtelGps->getLocTick = qtelPtr->getTick();
  if (AT_Command(&qtelPtr->atCmd, "+QGPSLOC", 1, paramData, 11, respData) != AT_OK)
    return QTEL_ERROR;

  qtelGps->data.latitude  = respData[1].value.floatNumber;
  qtelGps->data.longitude = respData[2].value.floatNumber;
  qtelGps->data.altitude  = (respData[5].value.number == 3)? respData[4].value.floatNumber: 0.0;
  qtelGps->data.HDOP      = respData[3].value.floatNumber;
  qtelGps->data.COG       = respData[6].value.floatNumber;
  qtelGps->data.speed     = respData[7].value.floatNumber;
  qtelGps->data.satelliteNumber = respData[10].value.number;

  return QTEL_OK;
}


#if QTEL_DEBUG_GPSNMEA
static QTEL_Status_t getNMEA(QTEL_GPS_HandlerTypeDef *qtelGps, QTEL_GPS_NMEAFormatType_t nmeaType)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;


  AT_Data_t paramData[1];
  AT_Data_t respData[1];

  switch (nmeaType) {
  case QTEL_GPS_GGA:
    AT_DataSetBuffer(&respData[0], qtelGps->nmea.GGA, 128);
    AT_DataSetString(&paramData[0], "GGA");
    break;

  case QTEL_GPS_RMC:
    AT_DataSetBuffer(&respData[0], qtelGps->nmea.RMC, 128);
    AT_DataSetString(&paramData[0], "RMC");
    break;

  case QTEL_GPS_GSV:
    AT_DataSetBuffer(&respData[0], qtelGps->nmea.GSV, 128);
    AT_DataSetString(&paramData[0], "GSV");
    break;

  case QTEL_GPS_GSA:
    AT_DataSetBuffer(&respData[0], qtelGps->nmea.GSA, 128);
    AT_DataSetString(&paramData[0], "GSA");
    break;

  case QTEL_GPS_VTG:
    AT_DataSetBuffer(&respData[0], qtelGps->nmea.VTG, 128);
    AT_DataSetString(&paramData[0], "VTG");
    break;

  case QTEL_GPS_GNS:
    AT_DataSetBuffer(&respData[0], qtelGps->nmea.GNS, 128);
    AT_DataSetString(&paramData[0], "GNS");
    break;

  default: return QTEL_ERROR;
  }


  if (AT_CommandSingleResp(&qtelPtr->atCmd, "+QGPSGNMEA", 1, paramData, respData) != AT_OK)
    return QTEL_ERROR;

  return QTEL_OK;
}
#endif /* QTEL_DEBUG_GPSNMEA */


static void parseTimeStr(QTEL_Datetime_t *dst, const char *src)
{
  uint8_t *dtbytes = (uint8_t*) dst;
  uint8_t len = (uint8_t) sizeof(QTEL_Datetime_t);
  uint8_t isParsing = 0;

  while (*src && len > 0) {
    if (*src >= '0' && *src <= '9') {
      if (!isParsing) {
        isParsing = 1;
        *dtbytes = (atoi(src) - ((dtbytes == &dst->year)? 2000: 0));
        dtbytes++;
        len--;
      }
    }
    else {
      isParsing = 0;
    }

    src++;
  }
}
#endif /* QTEL_EN_FEATURE_GPS */
