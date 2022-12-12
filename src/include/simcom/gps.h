/*
 * gps.h
 *
 *  Created on: Nov 14, 2022
 *      Author: janoko
 */

#ifndef SIMCOM_7600E_GPS_H_
#define SIMCOM_7600E_GPS_H_

#include "conf.h"
#if SIM_EN_FEATURE_GPS

#include "types.h"
#include <lwgps/lwgps.h>

#ifndef SIM_GPS_TMP_BUF_SIZE
#define SIM_GPS_TMP_BUF_SIZE 128
#endif

#define SIM_GPS_RPT_GPGGA 0x0001
#define SIM_GPS_RPT_GPRMC 0x0002
#define SIM_GPS_RPT_GPGSV 0x0004
#define SIM_GPS_RPT_GPGSA 0x0008
#define SIM_GPS_RPT_GPVTG 0x0010
#define SIM_GPS_RPT_PQXFI 0x0020
#define SIM_GPS_RPT_GLGSV 0x0040
#define SIM_GPS_RPT_GNGSA 0x0080
#define SIM_GPS_RPT_GNGNS 0x0100


enum {
  SIM_GPS_STATE_NON_ACTIVE,
  SIM_GPS_STATE_SETUP,
  SIM_GPS_STATE_ACTIVE,
};


typedef enum {
  SIM_GPS_MODE_STANDALONE = 1,
  SIM_GPS_MODE_UE_BASED,
  SIM_GPS_MODE_UE_ASISTED,
} SIM_GPS_Mode_t;

typedef enum {
  SIM_GPS_MEARATE_1HZ,
  SIM_GPS_NMEARATE_10HZ,
} SIM_GPS_NMEARate_t;

typedef enum {
  SIM_GPS_METHOD_CONTROL_PLANE,
  SIM_GPS_METHOD_USER_PLANE,
} SIM_GPS_MOAGPS_Method_t;

typedef enum {
  SIM_GPS_ANT_PASSIVE,
  SIM_GPS_ANT_ACTIVE,
} SIM_GPS_ANT_Mode_t;

typedef struct {
  uint16_t                accuracy;
  SIM_GPS_ANT_Mode_t      antenaMode;
  uint8_t                 isAutoDownloadXTRA;
  SIM_GPS_NMEARate_t      outputRate;
  uint8_t                 reportInterval;
  uint16_t                NMEA;
  SIM_GPS_MOAGPS_Method_t MOAGPS_Method;
  char                    *agpsServer;
  uint8_t                 isAgpsServerSecure;
} SIM_GPS_Config_t;

typedef struct SIM_GPS_HandlerTypeDef {
  void              *hsim;
  uint8_t           state;
  uint32_t          stateTick;
  uint8_t           isConfigured;
  SIM_GPS_Config_t  config;
  lwgps_t           lwgps;
} SIM_GPS_HandlerTypeDef;

SIM_Status_t SIM_GPS_Init(SIM_GPS_HandlerTypeDef*, void *hsim);
void         SIM_GPS_SetupConfig(SIM_GPS_HandlerTypeDef *hsimGps, SIM_GPS_Config_t *config);
void         SIM_GPS_SetState(SIM_GPS_HandlerTypeDef *hsimGps, uint8_t newState);
void         SIM_GPS_OnNewState(SIM_GPS_HandlerTypeDef *hsimGps);
SIM_Status_t SIM_GPS_SetupConfiguration(SIM_GPS_HandlerTypeDef*, SIM_GPS_Config_t*);
void         SIM_GPS_Loop(SIM_GPS_HandlerTypeDef*);
SIM_Status_t SIM_GPS_Activate(SIM_GPS_HandlerTypeDef*);

#endif /* SIM_EN_FEATURE_GPS */
#endif /* SIMCOM_7600E_GPS_H_ */
