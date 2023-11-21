#ifndef QUECTEL_EC25_H
#define QUECTEL_EC25_H

#include <quectel-ec25/conf.h>
#include <quectel-ec25/types.h>
#include <quectel-ec25/debug.h>
#include <quectel-ec25/net.h>
#include <quectel-ec25/ntp.h>
#include <quectel-ec25/http.h>
#include <quectel-ec25/gps.h>
#include <quectel-ec25/file.h>
#include <quectel-ec25/socket.h>
#include <at-command.h>

#define QTEL_STATUS_ATOK            0x01
#define QTEL_STATUS_CONFIGURED      0x02
#define QTEL_STATUS_SIM_READY       0x04
#define QTEL_STATUS_NET_REGISTERED  0x08
#define QTEL_STATUS_GPRS_REGISTERED 0x10
#define QTEL_STATUS_UART_READING    0x20
#define QTEL_STATUS_UART_WRITING    0x40
#define QTEL_STATUS_CMD_RUNNING     0x80

typedef enum {
  QTEL_STATE_NON_ACTIVE,
  QTEL_STATE_REBOOT,
  QTEL_STATE_READY,
  QTEL_STATE_CHECK_AT,
  QTEL_STATE_CONFIGURATION,
  QTEL_STATE_CHECK_SIMCARD,
  QTEL_STATE_CHECK_NETWORK,
  QTEL_STATE_ACTIVE,
} QTEL_State_t;


typedef struct QTEL_HandlerTypeDef {
  uint32_t            key;
  AT_HandlerTypeDef   atCmd;

  uint8_t             status;
  QTEL_State_t        state;
  uint8_t             events;
  uint8_t             errors;
  uint8_t             signal; // 0 - 100

  struct {
    uint32_t init;
    uint32_t changedState;
    uint32_t checksignal;
  } tick;

  QTEL_Status_t (*resetPower)(void);
  void (*delay)(uint32_t ms);
  uint32_t (*getTick)(void);

  struct {
    int (*read)(uint8_t *dst, uint16_t sz);
    int (*readline)(uint8_t *dst, uint16_t sz);
    int (*readinto)(void *buffer, uint16_t sz);
    int (*write)(const uint8_t *src, uint16_t sz);
  } serial;

  struct {
    AT_Status_t (*mutexLock)(uint32_t timeout);
    AT_Status_t (*mutexUnlock)(void);
    AT_Status_t (*eventSet)(uint32_t events);
    AT_Status_t (*eventWait)(uint32_t waitEvents, uint32_t *onEvents, uint32_t timeout);
    AT_Status_t (*eventClear)(uint32_t events);
  } rtos;

  uint8_t network_status;
  uint8_t GPRS_network_status;
  const char *operator;
  char registeredOperator[QTEL_OPERATOR_BUFFER_SIZE+1];
  char SIM_SN[QTEL_SIM_SN_BUFFER_SIZE+1];
  char SIM_IMEI[QTEL_SIM_IMEI_BUFFER_SIZE+1];
  char iccid[QTEL_ICCID_BUFFER_SIZE+1];

  struct {
    void (*onReady)(void);
    void (*onActive)(void);
  } callbacks;

  #if QTEL_EN_FEATURE_NET
  QTEL_NET_HandlerTypeDef net;
  #endif /* QTEL_EN_FEATURE_NET */

  #if QTEL_EN_FEATURE_NTP
  QTEL_NTP_HandlerTypeDef ntp;
  #endif /* QTEL_EN_FEATURE_NTP */

  #if QTEL_EN_FEATURE_SOCKET
  QTEL_Socket_HandlerTypeDef socketManager;
  #endif

  #if QTEL_EN_FEATURE_HTTP
  QTEL_HTTP_HandlerTypeDef http;
  #endif

  #if QTEL_EN_FEATURE_GPS
  QTEL_GPS_HandlerTypeDef gps;
  #endif

  #if QTEL_EN_FEATURE_FILE
  QTEL_FILE_HandlerTypeDef file;
  #endif

  // Buffers
  uint8_t  respBuffer[QTEL_RESP_BUFFER_SIZE];
  uint16_t respBufferLen;

  char     cmdBuffer[QTEL_CMD_BUFFER_SIZE];
  uint16_t cmdBufferLen;

  uint8_t  isRespConnectHandle;
  uint8_t  respConnectBuffer[QTEL_RESP_CONNECT_BUFFER_SIZE];
  uint16_t respConnectBufferLen;
} QTEL_HandlerTypeDef;


QTEL_Status_t QTEL_Init(QTEL_HandlerTypeDef*);

// Threads
void QTEL_Thread_Run(QTEL_HandlerTypeDef*);
void QTEL_Thread_ATCHandler(QTEL_HandlerTypeDef*);

QTEL_Status_t QTEL_Start(QTEL_HandlerTypeDef*);
QTEL_Status_t QTEL_Reboot(QTEL_HandlerTypeDef*);
QTEL_Status_t QTEL_ResetSIM(QTEL_HandlerTypeDef*);
void QTEL_SetState(QTEL_HandlerTypeDef*, uint8_t newState);

#endif /* QUECTEL_EC25_H */
