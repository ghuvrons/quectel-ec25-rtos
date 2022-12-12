/*
 * socket-client.h
 *
 *  Created on: Nov 10, 2022
 *      Author: janoko
 */

#ifndef SIMCOM_7600E_SOCKET_CLIENT_H_
#define SIMCOM_7600E_SOCKET_CLIENT_H_

#include "conf.h"
#if SIM_EN_FEATURE_SOCKET

#include "types.h"

#define SIM_SOCK_UDP    0
#define SIM_SOCK_TCPIP  1

#define SIM_SOCK_EVENT_ON_OPENED        0x01
#define SIM_SOCK_EVENT_ON_OPENING_ERROR 0x02
#define SIM_SOCK_EVENT_ON_RECEIVED      0x04
#define SIM_SOCK_EVENT_ON_CLOSED        0x08

enum {
  SIM_SOCK_CLIENT_STATE_CLOSE,
  SIM_SOCK_CLIENT_STATE_WAIT_NETOPEN,
  SIM_SOCK_CLIENT_STATE_OPENING,
  SIM_SOCK_CLIENT_STATE_OPEN_PENDING,
  SIM_SOCK_CLIENT_STATE_OPEN,
};


typedef struct {
  struct SIM_Socket_HandlerTypeDef *socketManager;
  uint8_t     state;
  uint8_t     events;               // Events flag
  int8_t      linkNum;
  uint8_t     type;                 // SIM_SOCK_UDP or SIM_SOCK_TCPIP

  // configuration
  struct {
    uint32_t timeout;
    uint8_t  autoReconnect;
    uint16_t reconnectingDelay;
  } config;

  // tick register for delay and timeout
  struct {
    uint32_t reconnDelay;
    uint32_t connecting;
  } tick;

  // server
  char     host[64];
  uint16_t port;

  void *buffer;

  // listener
  struct {
    void (*onConnecting)(void);
    void (*onConnected)(void);
    void (*onConnectError)(void);
    void (*onClosed)(void);
    void (*onReceived)(void *buffer);
  } listeners;
} SIM_SocketClient_t;


// SOCKET
SIM_Status_t  SIM_SockClient_Init(SIM_SocketClient_t*, const char *host, uint16_t port, void *buffer);
SIM_Status_t  SIM_SockClient_CheckEvents(SIM_SocketClient_t*);
SIM_Status_t  SIM_SockClient_OnNetOpened(SIM_SocketClient_t*);
SIM_Status_t  SIM_SockClient_Loop(SIM_SocketClient_t*);
void          SIM_SockClient_SetBuffer(SIM_SocketClient_t*, void *buffer);
SIM_Status_t  SIM_SockClient_Open(SIM_SocketClient_t*, void*);
SIM_Status_t  SIM_SockClient_Close(SIM_SocketClient_t*);
uint16_t      SIM_SockClient_SendData(SIM_SocketClient_t*, uint8_t *data, uint16_t length);


#endif /* SIM_EN_FEATURE_SOCKET */
#endif /* SIMCOM_7600E_SOCKET_CLIENT_H_ */
