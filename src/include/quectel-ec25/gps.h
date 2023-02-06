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

#define QTEL_AGPS_MODE_STANDALONE                 0x00000001
#define QTEL_AGPS_MODE_UP_MS_BASED                0x00000002
#define QTEL_AGPS_MODE_UP_MS_ASSISTED             0x00000004
#define QTEL_AGPS_MODE_CP_MS_BASED_2G             0x00000008
#define QTEL_AGPS_MODE_CP_MS_ASSISTED_2G          0x00000010
#define QTEL_AGPS_MODE_CP_MS_BASED_3G             0x00000020
#define QTEL_AGPS_MODE_CP_MS_ASSISTED_3G          0x00000040
#define QTEL_AGPS_MODE_UP_NMEA_RPT                0x00000080
#define QTEL_AGPS_MODE_UP_MS_BASED_4G             0x00000100
#define QTEL_AGPS_MODE_UP_MS_ASSISTED_4G          0x00000200
#define QTEL_AGPS_MODE_CP_MS_BASED_4G             0x00000400
#define QTEL_AGPS_MODE_CP_MS_ASSISTED_4G          0x00000800
#define QTEL_AGPS_MODE_EN_AUTO_FALLBACK_SUPL_MSB  0x00010000
#define QTEL_AGPS_MODE_AGLONASS_UP_MSB_3G         0x00020000
#define QTEL_AGPS_MODE_AGLONASS_UP_MSA_3G         0x00040000
#define QTEL_AGPS_MODE_AGLONASS_CP_MSB_3G         0x00080000
#define QTEL_AGPS_MODE_AGLONASS_CP_MSA_3G         0x00100000
#define QTEL_AGPS_MODE_AGLONASS_UP_MSB_4G         0x00200000
#define QTEL_AGPS_MODE_AGLONASS_UP_MSA_4G         0x00400000
#define QTEL_AGPS_MODE_AGLONASS_CP_MSB_4G         0x00800000
#define QTEL_AGPS_MODE_AGLONASS_CP_MSA_4G         0x01000000

#define QTEL_AGPS_PTC_USER_PLANE_LPP              0x01
#define QTEL_AGPS_PTC_CONTROL_PLANE_LPP           0x02
#define QTEL_AGLONASS_PTC_CONTROL_PLANE_RRLP      0x0001
#define QTEL_AGLONASS_PTC_CONTROL_PLANE_RRC       0x0002
#define QTEL_AGLONASS_PTC_CONTROL_PLANE_LPP       0x0004
#define QTEL_AGLONASS_PTC_USER_PLANE_RRLP         0x0100
#define QTEL_AGLONASS_PTC_USER_PLANE_LPP          0x0400


enum {
  QTEL_GPS_STATE_NON_ACTIVE,
  QTEL_GPS_STATE_SETUP,
  QTEL_GPS_STATE_WAIT_ONLINE,
  QTEL_GPS_STATE_ACTIVE,
};

typedef enum {
  QTEL_GPS_MEARATE_1HZ = 1,
  QTEL_GPS_NMEARATE_10HZ = 10,
} QTEL_GPS_NMEARate_t;

typedef enum {
  QTEL_GPS_USER_PLANE,
  QTEL_GPS_USER_PLANE_SSL,
  QTEL_GPS_CONTROL_PLANE,
} QTEL_GPS_Plane_Mode_t;

typedef enum {
  QTEL_GPS_SUPL_V1 = 1,
  QTEL_GPS_SUPL_V2,
} QTEL_GPS_SUPL_Version_t;


typedef struct {
  uint16_t                  accuracy;
  QTEL_GPS_NMEARate_t       outputRate;
  uint16_t                  NMEA;
  QTEL_GPS_Plane_Mode_t     planeMode;
  uint32_t                  AGPS_Mode;
  uint8_t                   AGPS_Protocols;
  uint16_t                  AGLONASS_Protocols;

  struct {
    QTEL_GPS_SUPL_Version_t version;
    char                    *server;
  } supl;

#if QTEL_EN_FEATURE_GPS_ONEXTRA
  struct {
    const char  *dataURL;
  } oneXTRA;
#endif
} QTEL_GPS_Config_t;

typedef struct QTEL_GPS_HandlerTypeDef {
  void                *qtel;
  uint8_t             state;
  uint32_t            stateTick;
  uint32_t            getLocTick;
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
