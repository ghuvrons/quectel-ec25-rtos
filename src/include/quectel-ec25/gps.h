/*
 * gps.h
 *
 *  Created on: Nov 14, 2022
 *      Author: janoko
 */

#ifndef QUECTEL_EC25_GPS_H_
#define QUECTEL_EC25_GPS_H_

#include <quectel-ec25/conf.h>
#if QTEL_EN_FEATURE_GPS

#include <quectel-ec25/types.h>
#include <lwgps/lwgps.h>

#ifndef QTEL_GPS_TMP_BUF_SIZE
#define QTEL_GPS_TMP_BUF_SIZE 128
#endif

#define QTEL_GPS_RPT_GPGGA 0x0001
#define QTEL_GPS_RPT_GPRMC 0x0002
#define QTEL_GPS_RPT_GPGSV 0x0004
#define QTEL_GPS_RPT_GPGSA 0x0008
#define QTEL_GPS_RPT_GPVTG 0x0010
#define QTEL_GPS_RPT_PQXFI 0x0020
#define QTEL_GPS_RPT_GLGSV 0x0040
#define QTEL_GPS_RPT_GNGSA 0x0080
#define QTEL_GPS_RPT_GNGNS 0x0100


enum {
  QTEL_GPS_STATE_NON_ACTIVE,
  QTEL_GPS_STATE_SETUP,
  QTEL_GPS_STATE_ACTIVE,
};


typedef enum {
  QTEL_GPS_MODE_STANDALONE = 1,
  QTEL_GPS_MODE_UE_BASED,
  QTEL_GPS_MODE_UE_ASISTED,
} QTEL_GPS_Mode_t;

typedef enum {
  QTEL_GPS_MEARATE_1HZ,
  QTEL_GPS_NMEARATE_10HZ,
} QTEL_GPS_NMEARate_t;

typedef enum {
  QTEL_GPS_METHOD_CONTROL_PLANE,
  QTEL_GPS_METHOD_USER_PLANE,
} QTEL_GPS_MOAGPS_Method_t;

typedef enum {
  QTEL_GPS_ANT_PASSIVE,
  QTEL_GPS_ANT_ACTIVE,
} QTEL_GPS_ANT_Mode_t;

typedef struct {
  uint16_t                  accuracy;
  QTEL_GPS_ANT_Mode_t       antenaMode;
  uint8_t                   isAutoDownloadXTRA;
  QTEL_GPS_NMEARate_t       outputRate;
  uint8_t                   reportInterval;
  uint16_t                  NMEA;
  QTEL_GPS_MOAGPS_Method_t  MOAGPS_Method;
  char                      *agpsServer;
  uint8_t                   isAgpsServerSecure;
} QTEL_GPS_Config_t;

typedef struct QTEL_GPS_HandlerTypeDef {
  void                *qtel;
  uint8_t             state;
  uint32_t            stateTick;
  uint8_t             isConfigured;
  QTEL_GPS_Config_t   config;
  lwgps_t             lwgps;
} QTEL_GPS_HandlerTypeDef;

QTEL_Status_t QTEL_GPS_Init(QTEL_GPS_HandlerTypeDef*, void *qtelPtr);
void          QTEL_GPS_SetupConfig(QTEL_GPS_HandlerTypeDef*, QTEL_GPS_Config_t*);
void          QTEL_GPS_SetState(QTEL_GPS_HandlerTypeDef*, uint8_t newState);
void          QTEL_GPS_OnNewState(QTEL_GPS_HandlerTypeDef*);
QTEL_Status_t QTEL_GPS_SetupConfiguration(QTEL_GPS_HandlerTypeDef*, QTEL_GPS_Config_t*);
void          QTEL_GPS_Loop(QTEL_GPS_HandlerTypeDef*);
QTEL_Status_t QTEL_GPS_Activate(QTEL_GPS_HandlerTypeDef*);

#endif /* QTEL_EN_FEATURE_GPS */
#endif /* QUECTEL_EC25_GPS_H_ */
