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
#include <quectel-ec25/utils.h>
#include <stdlib.h>
#include <string.h>

#define QTEL_GPS_CONFIG_KEY   0xAE
#define QTEL_ONEXTRA_TMP_FILE "RAM:xtra2.bin"
#define QTEL_SUPL_CA_FILE     "RAM:modem_supl_ca.cert"

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
      .version  = QTEL_GPS_SUPL_V1,
      .server   = "supl.google.com:7276",
  },
  .oneXTRA = {
      .dataURL = "http://xtrapath4.izatcloud.net/xtra2.bin",
  },
};

static const char *SUPL_CA =
"-----BEGIN CERTIFICATE-----"
"MIIFVTCCBD2gAwIBAgIQFi0c9HaFGosKq1bjw1JgzDANBgkqhkiG9w0BAQsFADBG"
"MQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExM"
"QzETMBEGA1UEAxMKR1RTIENBIDFDMzAeFw0yMzAxMDkwODE5MTRaFw0yMzA0MDMw"
"ODE5MTNaMBoxGDAWBgNVBAMTD3N1cGwuZ29vZ2xlLmNvbTCCASIwDQYJKoZIhvcN"
"AQEBBQADggEPADCCAQoCggEBALAJK6RJRgmKdYRhBkYyaIuEEbqgdRaIkv3Lapfh"
"K5nddkoFuyq4Ite5z6VnInYSXJs8um5qcOriklb6xkoEIagnfRVCgi80VSgoL7iB"
"0xwA69B+AyesBZNo9WFTZL5Ta9rtKfHQndKNpMTM1EmX5Yg/MP9DCcyErrVHop6X"
"PU1BIm/3pnxwFxtnm5ZHNcVO3Ytei+s5dab/S/uwxwxjMArfeGchLFYDNDcPAsdt"
"hZJ8drjFWQFcc6uDISaSZvPdl0HuF70cIZAyLlTxF1yKBOOrzmEL1ivHzplUKyz3"
"Pas1fv5Ei3wZqpjAE2MVkFen11JaU3iMViFxhfEaMTVz9ZkCAwEAAaOCAmkwggJl"
"MA4GA1UdDwEB/wQEAwIFoDATBgNVHSUEDDAKBggrBgEFBQcDATAMBgNVHRMBAf8E"
"AjAAMB0GA1UdDgQWBBTflouleGHCC6WJQwe4pPrDVn95hzAfBgNVHSMEGDAWgBSK"
"dH+vhc3ulc09nNDiRhTzcTUdJzBqBggrBgEFBQcBAQReMFwwJwYIKwYBBQUHMAGG"
"G2h0dHA6Ly9vY3NwLnBraS5nb29nL2d0czFjMzAxBggrBgEFBQcwAoYlaHR0cDov"
"L3BraS5nb29nL3JlcG8vY2VydHMvZ3RzMWMzLmRlcjAaBgNVHREEEzARgg9zdXBs"
"Lmdvb2dsZS5jb20wIQYDVR0gBBowGDAIBgZngQwBAgEwDAYKKwYBBAHWeQIFAzA8"
"BgNVHR8ENTAzMDGgL6AthitodHRwOi8vY3Jscy5wa2kuZ29vZy9ndHMxYzMvbW9W"
"RGZJU2lhMmsuY3JsMIIBBQYKKwYBBAHWeQIEAgSB9gSB8wDxAHYArfe++nz/EMiL"
"nT2cHj4YarRnKV3PsQwkyoWGNOvcgooAAAGFldOuSAAABAMARzBFAiEAvuGHxp2h"
"cIJIL+Z4AmcbIeI4o0uFiWPuBYt/OZht1RcCIGzucwobkpcMrN9rKpiwr7L5XA0z"
"HfyWgHaF1ywHMEesAHcAtz77JN+cTbp18jnFulj0bF38Qs96nzXEnh0JgSXttJkA"
"AAGFldOuGAAABAMASDBGAiEA8WtfQcSdVBey59jKQvk50tufvxg7456y2BBZ5/yL"
"mIoCIQDUAUg7ADuejictmpfmN7zXqwDnr2tWNtEZal9x4a+4bDANBgkqhkiG9w0B"
"AQsFAAOCAQEA6D1OJ68JA5CSvDEH6fT4jMKjHZxslAI/47Tvz+y4j/VFljX9DvvL"
"ckQasRa3Jc7fooVdIai+BX1Ckwdh5fVsmqkS3fS0Zhzi5W68/3qGTaf2dq0OiyDl"
"EikfcqiRoKlkKA60ML2c9wxFPgUq3oxyAvFpF00WDdH5ysPxj+i4wn37ZA3pgDan"
"IzZhr5St595oowiBlsficG6pzgxtm4oIfIqQlZHkZBJzJH/nX4bvloVkIPTgGxwS"
"TZglE2WyTld27mnYH031qBgoWYeJSjjsmT/0L8d2zk4MpDcE/zZjm1hcgQwOhDKq"
"hGfGSpEgWcJTfPPsFFCnJ34WfhSMPt+IQg=="
"-----END CERTIFICATE-----";

static QTEL_Status_t setConfiguration(QTEL_GPS_HandlerTypeDef*);
static QTEL_Status_t activate(QTEL_GPS_HandlerTypeDef*, uint8_t isActivate);
static QTEL_Status_t configureOneXTRA(QTEL_GPS_HandlerTypeDef*);
static QTEL_Status_t acquirePosition(QTEL_GPS_HandlerTypeDef*);
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
    if (setConfiguration(qtelGps) != QTEL_OK) {
      QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_NON_ACTIVE);
      break;
    }
    if (activate(qtelGps, 1) != QTEL_OK) {
      QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_NON_ACTIVE);
      break;
    }
    QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_ACTIVE);
    break;

  case QTEL_GPS_STATE_ACTIVE:
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
    if (qtelPtr->net.state > QTEL_NET_STATE_CHECK_GPRS) {
      if (QTEL_IsTimeout(qtelPtr, qtelGps->stateTick, 2000)) {
        QTEL_GPS_SetState(qtelGps, QTEL_GPS_STATE_SETUP);
      }
    }
    break;

  case QTEL_GPS_STATE_ACTIVE:
    if (QTEL_IsTimeout(qtelPtr, qtelGps->getLocTick, 2000)) {
      qtelGps->getLocTick = qtelPtr->getTick();
      acquirePosition(qtelGps);
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
  int fn = -1;
  AT_Data_t paramData[3];

  AT_DataSetString(&paramData[0], "gnssconfig");
  AT_DataSetNumber(&paramData[1], 1);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetString(&paramData[0], "plane");
  AT_DataSetNumber(&paramData[1], qtelGps->config.planeMode);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetString(&paramData[0], "suplver");
  AT_DataSetNumber(&paramData[1], qtelGps->config.supl.version);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetString(&paramData[0], "agpsposmode");
  AT_DataSetNumber(&paramData[1], qtelGps->config.AGPS_Mode);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetString(&paramData[0], "agnssprotocol");
  AT_DataSetNumber(&paramData[1], qtelGps->config.AGPS_Protocols);
  AT_DataSetNumber(&paramData[2], qtelGps->config.AGLONASS_Protocols);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 3, paramData, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetString(&paramData[0], "fixfreq");
  AT_DataSetNumber(&paramData[1], qtelGps->config.outputRate);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSCFG", 2, paramData, 0, 0) != AT_OK) goto endCmd;

  if (qtelGps->config.supl.server != 0) {
    AT_DataSetString(&paramData[0], qtelGps->config.supl.server);
    if (AT_Command(&qtelPtr->atCmd, "+QGPSSUPLURL", 1, paramData, 0, 0) != AT_OK) goto endCmd;

    QTEL_FILE_RemoveFile(&qtelPtr->file, QTEL_SUPL_CA_FILE);

    if (QTEL_FILE_Open(&qtelPtr->file, QTEL_SUPL_CA_FILE, &fn) != QTEL_OK)
      goto endCmd;

    if (QTEL_FILE_Write(&qtelPtr->file, fn, (const uint8_t*) SUPL_CA, strlen(SUPL_CA)) != QTEL_OK)
      goto endCmd;

    if (QTEL_FILE_Close(&qtelPtr->file, fn) != QTEL_OK)
      goto endCmd;

    AT_DataSetString(&paramData[0], QTEL_SUPL_CA_FILE);
    if (AT_Command(&qtelPtr->atCmd, "+QGPSSUPLCA", 1, paramData, 0, 0) != AT_OK)
      goto endCmd;

  }

#if QTEL_EN_FEATURE_GPS_ONEXTRA
  status = configureOneXTRA(qtelGps);
  if (status != QTEL_OK) goto endCmd;
#endif /* QTEL_EN_FEATURE_GPS_ONEXTRA */

  status = QTEL_OK;

endCmd:
  return status;
}

static QTEL_Status_t activate(QTEL_GPS_HandlerTypeDef *qtelGps, uint8_t isActivate)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;
  AT_Data_t paramData[1] = {
      AT_Number(2),
  };

  if (isActivate) {
    if (AT_Command(&qtelPtr->atCmd, "+QGPS", 1, paramData, 0, 0) != AT_OK)
      return QTEL_ERROR;
  }
  else {
    if (AT_Command(&qtelPtr->atCmd, "+QGPSEND", 0, 0, 0, 0) != AT_OK)
      return QTEL_ERROR;
  }

  return QTEL_OK;
}


#if QTEL_EN_FEATURE_GPS_ONEXTRA
static QTEL_Status_t configureOneXTRA(QTEL_GPS_HandlerTypeDef *qtelGps)
{
  QTEL_HandlerTypeDef *qtelPtr = qtelGps->qtel;
  QTEL_HTTP_Response_t resp;
  AT_Data_t paramData[5];
  uint8_t respstr[24];
  AT_Data_t respData[2] = {
      AT_Number(0),
      AT_Buffer(respstr, sizeof(respstr)),
  };

  // [TODO]: check xtra data and time. if it is still available skip configure

  if (qtelGps->config.oneXTRA.dataURL != 0) {
    AT_DataSetNumber(&paramData[0], 1);
    if (AT_Command(&qtelPtr->atCmd, "+QGPSXTRA", 1, paramData, 0, 0) != AT_OK)
      return QTEL_ERROR;

    if (AT_Check(&qtelPtr->atCmd, "+QGPSXTRADATA", 2, respData) != AT_OK)
      return QTEL_ERROR;

    if (respData[0].type == AT_NUMBER
        && respData[0].value.number > 60)
    {
      return QTEL_OK;
    }

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
    AT_DataSetString(&paramData[1], "2023/02/08,08:30:30");
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
  AT_Data_t paramData[1];

  AT_DataSetNumber(&paramData[0], 2);
  if (AT_Command(&qtelPtr->atCmd, "+QGPSLOC", 1, paramData, 0, 0) != AT_OK)
    return QTEL_ERROR;

  return QTEL_OK;
}


static void onGetNMEA(void *app, uint8_t *data, uint16_t len)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;
  *(data+len) = 0;
}

#endif /* QTEL_EN_FEATURE_GPS */
