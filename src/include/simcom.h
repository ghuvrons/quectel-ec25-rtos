#ifndef SIMCOM_7600E_H
#define SIMCOM_7600E_H

#include "simcom/conf.h"
#include "simcom/types.h"
#include "simcom/debug.h"
#include "simcom/net.h"
#include "simcom/ntp.h"
#include "simcom/http.h"
#include "simcom/gps.h"
#include "simcom/file.h"
#include "simcom/socket.h"
#include <at-command.h>

#define SIM_STATUS_ACTIVE           0x01
#define SIM_STATUS_ROAMING          0x08
#define SIM_STATUS_UART_READING     0x10
#define SIM_STATUS_UART_WRITING     0x20
#define SIM_STATUS_CMD_RUNNING      0x40

enum {
 SIM_STATE_NON_ACTIVE,
 SIM_STATE_CHECK_AT,
 SIM_STATE_CHECK_SIMCARD,
 SIM_STATE_CHECK_NETWORK,
 SIM_STATE_ACTIVE,
};


typedef struct SIM_HandlerTypeDef {
  uint32_t            key;
  AT_HandlerTypeDef   atCmd;

  uint8_t             status;
  uint8_t             state;
  uint8_t             events;
  uint8_t             errors;

  uint8_t             signal;

  struct {
    uint32_t init;
    uint32_t changedState;
    uint32_t checksignal;
  } tick;


  void (*delay)(uint32_t ms);
  uint32_t (*getTick)(void);

  struct {
    int (*read)(uint8_t *dst, uint16_t sz);
    int (*readline)(uint8_t *dst, uint16_t sz);
    int (*readinto)(void *buffer, uint16_t sz);
    int (*write)(uint8_t *src, uint16_t sz);
  } serial;

  struct {
    AT_Status_t (*mutexLock)(uint32_t timeout);
    AT_Status_t (*mutexUnlock)(void);
    AT_Status_t (*eventSet)(uint32_t events);
    AT_Status_t (*eventWait)(uint32_t waitEvents, uint32_t *onEvents, uint32_t timeout);
    AT_Status_t (*eventClear)(uint32_t events);
  } rtos;

  uint8_t network_status;

  #if SIM_EN_FEATURE_NET
  SIM_NET_HandlerTypeDef net;
  #endif /* SIM_EN_FEATURE_NET */

  #if SIM_EN_FEATURE_NTP
  SIM_NTP_HandlerTypeDef ntp;
  #endif /* SIM_EN_FEATURE_NTP */

  #if SIM_EN_FEATURE_SOCKET
  SIM_Socket_HandlerTypeDef socketManager;
  #endif

  #if SIM_EN_FEATURE_HTTP
  SIM_HTTP_HandlerTypeDef http;
  #endif

  #if SIM_EN_FEATURE_GPS
  SIM_GPS_HandlerTypeDef gps;
  #endif

  #if SIM_EN_FEATURE_FILE
  SIM_FILE_HandlerTypeDef file;
  #endif

  // Buffers
  uint8_t  respBuffer[SIM_RESP_BUFFER_SIZE];
  uint16_t respBufferLen;

  char     cmdBuffer[SIM_CMD_BUFFER_SIZE];
  uint16_t cmdBufferLen;
} SIM_HandlerTypeDef;


SIM_Status_t SIM_Init(SIM_HandlerTypeDef*);

// Threads
void SIM_Thread_Run(SIM_HandlerTypeDef*);
void SIM_Thread_ATCHandler(SIM_HandlerTypeDef*);

void SIM_SetState(SIM_HandlerTypeDef*, uint8_t newState);

#endif /* SIMCOM_7600E_H */
