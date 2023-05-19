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

#define QTEL_GPS_CONFIG_KEY   0xAE
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
      .dataURL = "http://xtrapath4.izatcloud.net/xtra2.bin",
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
    if (setConfiguration(qtelGps) != QTEL_OK) {
      QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_NON_ACTIVE);
      break;
    }

#if QTEL_EN_FEATURE_GPS_ONEXTRA
    if (!QTEL_IS_STATUS(&qtelPtr->ntp, QTEL_NTP_WAS_SYNCED)) {
      QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_WAITING_NTP);
      break;
    }
    if (configureOneXTRA(qtelGps) != QTEL_OK) {
      break;
    }
#endif /* QTEL_EN_FEATURE_GPS_ONEXTRA */

    if (startGPS(qtelGps, QTEL_GPS_MS_BASED) != QTEL_OK) {
      QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_NON_ACTIVE);
      break;
    }

    QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_FIXING);
    break;

  case QTEL_GPS_STATE_FIXING:
    qtelGps->getLocTick = qtelGps->stateTick;
    break;

  case QTEL_GPS_STATE_FIXED:
    qtelGps->getLocTick = qtelGps->stateTick;
    if (qtelGps->mode != QTEL_GPS_STANDALONE) {
      // change mode to stand-alone
      startGPS(qtelGps, QTEL_GPS_STANDALONE);
    }
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
    if (qtelPtr->state >= QTEL_STATE_ACTIVE) {
      if (QTEL_IsTimeout(qtelPtr, qtelGps->stateTick, 2000)) {
        QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_SETUP);
      }
    }
    break;

  case QTEL_GPS_STATE_WAITING_NTP:
    if (QTEL_IsTimeout(qtelPtr, qtelGps->stateTick, 1000)) {
      qtelGps->stateTick = qtelPtr->getTick();

      if (QTEL_IS_STATUS(&qtelPtr->ntp, QTEL_NTP_WAS_SYNCED)) {
        QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_SETUP);
      }
    }
    break;

  case QTEL_GPS_STATE_FIXING:
    if (QTEL_IsTimeout(qtelPtr, qtelGps->getLocTick, 2000)) {
      qtelGps->getLocTick = qtelPtr->getTick();
      if (acquirePosition(qtelGps) == QTEL_OK) {
        QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_FIXED);
        break;
      }
    }

#if QTEL_EN_FEATURE_NET
    switch (qtelGps->mode) {
    case QTEL_GPS_STANDALONE:
      if (QTEL_IsTimeout(qtelPtr, qtelGps->stateTick, 60000)) {
        startGPS(qtelGps, QTEL_GPS_MS_BASED);
        qtelGps->stateTick = qtelPtr->getTick();
      }
      break;

    case QTEL_GPS_MS_BASED:
      if (QTEL_IsTimeout(qtelPtr, qtelGps->stateTick, 120000)) {
        startGPS(qtelGps, QTEL_GPS_MS_ASSISTED);
        qtelGps->stateTick = qtelPtr->getTick();
      }
      break;

    case QTEL_GPS_MS_ASSISTED:
      if (QTEL_IsTimeout(qtelPtr, qtelGps->stateTick, 60000)) {
        startGPS(qtelGps, QTEL_GPS_STANDALONE);
        qtelGps->stateTick = qtelPtr->getTick();
      }
      break;
    }
#endif
    break;


  case QTEL_GPS_STATE_FIXED:
    if (QTEL_IsTimeout(qtelPtr, qtelGps->getLocTick, 5000)) {
      qtelGps->getLocTick = qtelPtr->getTick();
      if (acquirePosition(qtelGps) != QTEL_OK) {
        startGPS(qtelGps, QTEL_GPS_MS_BASED);
        QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_FIXING);
        break;
      }
    }
    break;

  default: break;
  }

  return;
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
  };

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
      goto activateGPS;
    }
  }

  stopGPS(qtelGps);

  activateGPS:
  if (AT_Command(&qtelPtr->atCmd, "+QGPS", 1, paramData, 0, 0) != AT_OK)
    return QTEL_ERROR;

  qtelGps->mode = mode;
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
  uint8_t xtratimeStr[24];
  AT_Data_t respData[2] = {
      AT_Number(0),
      AT_Buffer(xtratimeStr, sizeof(xtratimeStr)),
  };


  if (qtelGps->config.oneXTRA.dataURL != 0) {
    AT_DataSetNumber(&paramData[0], 1);
    if (AT_Command(&qtelPtr->atCmd, "+QGPSXTRA", 1, paramData, 0, 0) != AT_OK)
      return QTEL_ERROR;

    if (AT_Check(&qtelPtr->atCmd, "+QGPSXTRADATA", 2, respData) != AT_OK)
      return QTEL_ERROR;

    if (respData[0].type == AT_NUMBER &&
        respData[1].type == AT_STRING &&
        respData[1].value.string != 0)
    {
      parseTimeStr(&xtratime, respData[1].value.string);
      QTEL_Datetime_AddSeconds(&xtratime, (respData[0].value.number * 60));


      QTEL_GetTime(qtelPtr, &currenttime);
      QTEL_Datetime_SetToUTC(&currenttime);

      // if currenttime > (xtratime (expiredtime) - 1 day)
      if (QTEL_Datetime_Diff(&currenttime, &xtratime) <= 1440)
      {
        return QTEL_OK;
      }

      snprintf((char*)xtratimeStr, 24, "%02d/%02d/%02d,%02d:%02d:%02d",
               ((int)currenttime.year) + 2000,
               (int) currenttime.month,
               (int) currenttime.day,
               (int) currenttime.hour,
               (int) currenttime.minute,
               (int) currenttime.second
               );
    }
    else return QTEL_OK;

    QTEL_FILE_RemoveFile(&qtelPtr->file, QTEL_ONEXTRA_TMP_FILE);
    if (QTEL_HTTP_DownloadAndSave(&qtelPtr->http,
                                  qtelGps->config.oneXTRA.dataURL,
                                  QTEL_ONEXTRA_TMP_FILE,
                                  &resp,
                                  60000) != QTEL_OK)
    {
      return QTEL_ERROR;
    }

    AT_DataSetNumber(&paramData[0], 0);
    AT_DataSetString(&paramData[1], (char*)xtratimeStr);
    AT_DataSetNumber(&paramData[2], 1);
    AT_DataSetNumber(&paramData[3], 1);
    AT_DataSetNumber(&paramData[4], 3500);
    if (AT_Command(&qtelPtr->atCmd, "+QGPSXTRATIME", 5, paramData, 0, 0) != AT_OK)
      return QTEL_ERROR;

    AT_DataSetString(&paramData[0], QTEL_ONEXTRA_TMP_FILE);
    if (AT_Command(&qtelPtr->atCmd, "+QGPSXTRADATA", 1, paramData, 0, 0) != AT_OK)
      return QTEL_ERROR;
  }

  return QTEL_OK;
}
#endif /* QTEL_EN_FEATURE_GPS_ONEXTRA */


static QTEL_Status_t acquirePosition(QTEL_GPS_HandlerTypeDef *qtelGps)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;

  struct {
    uint8_t utc[11];
    uint8_t date[7];
  } tmpBuffer;

  // [TODO] : gen resp of lat long and save to gps data.
  //

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
