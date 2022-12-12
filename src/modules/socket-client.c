/*
 * socket-client.c
 *
 *  Created on: Nov 10, 2022
 *      Author: janoko
 */

#include <quectel-ec25/socket-client.h>
#if QTEL_EN_FEATURE_SOCKET

#include "../include/quectel-ec25.h"
#include <quectel-ec25/socket.h>
#include <quectel-ec25/utils.h>
#include <string.h>


#define Get_Available_LinkNum(hsimsock, linkNum) {\
  for (int16_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {\
    if ((hsimsock)->sockets[i] == NULL) {\
      *(linkNum) = i;\
      break;\
    }\
  }\
}


static QTEL_Status_t sockOpen(QTEL_SocketClient_t *sock);
static uint8_t isSockConnected(QTEL_SocketClient_t *sock);
static QTEL_Status_t sockClose(QTEL_SocketClient_t *sock);


QTEL_Status_t SIM_SockClient_Init(QTEL_SocketClient_t *sock, const char *host, uint16_t port, void *buffer)
{
  char *sockIP = sock->host;
  while (*host != '\0') {
    *sockIP = *host;
    host++;
    sockIP++;
  }

  sock->port = port;

  if (sock->config.timeout == 0)
    sock->config.timeout = QTEL_SOCK_DEFAULT_TO;
  if (sock->config.reconnectingDelay == 0)
    sock->config.reconnectingDelay = 5000;

  sock->linkNum = -1;
  sock->buffer = buffer;
  if (sock->buffer == NULL)
    return QTEL_ERROR;

  sock->state = SIM_SOCK_CLIENT_STATE_CLOSE;
  return QTEL_OK;
}


QTEL_Status_t SIM_SockClient_OnNetOpened(QTEL_SocketClient_t *sock)
{
  if (sock->state == SIM_SOCK_CLIENT_STATE_WAIT_NETOPEN) {
    return sockOpen(sock);
  }
  return QTEL_OK;
}


QTEL_Status_t SIM_SockClient_CheckEvents(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *hsim = sock->socketManager->hsim;

  if (QTEL_BITS_IS(sock->events, SIM_SOCK_EVENT_ON_OPENED)) {
    QTEL_BITS_UNSET(sock->events, SIM_SOCK_EVENT_ON_OPENED);
    if (sock->listeners.onConnected) sock->listeners.onConnected();
  }
  if (QTEL_BITS_IS(sock->events, SIM_SOCK_EVENT_ON_CLOSED)) {
    QTEL_BITS_UNSET(sock->events, SIM_SOCK_EVENT_ON_CLOSED);
    if (sock->state == SIM_SOCK_CLIENT_STATE_OPEN_PENDING) {
      sockOpen(sock);
    } else {
      sock->state = SIM_SOCK_CLIENT_STATE_CLOSE;
      sock->tick.reconnDelay = hsim->getTick();
      if (sock->listeners.onClosed) sock->listeners.onClosed();
    }
  }
  return QTEL_OK;
}


QTEL_Status_t SIM_SockClient_Loop(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *hsim = sock->socketManager->hsim;

  switch (sock->state) {
  case SIM_SOCK_CLIENT_STATE_WAIT_NETOPEN:
    sockOpen(sock);
    break;

  case SIM_SOCK_CLIENT_STATE_OPENING:
    if (sock->tick.connecting && QTEL_IsTimeout(hsim, sock->tick.connecting, 30000)) {
      sock->state = SIM_SOCK_CLIENT_STATE_OPEN_PENDING;
      SIM_SockClient_Close(sock);
    }
    break;

  case SIM_SOCK_CLIENT_STATE_CLOSE:
    if (sock->tick.reconnDelay == 0) {

    }
    else if (QTEL_IsTimeout(hsim, sock->tick.reconnDelay, 2000)) {
      sockOpen(sock);
    }
    break;

  default: break;
  }

  return QTEL_OK;
}


void SIM_SockClient_SetBuffer(QTEL_SocketClient_t *sock, void *buffer)
{
  sock->buffer = buffer;
}


QTEL_Status_t SIM_SockClient_Open(QTEL_SocketClient_t *sock, void *hsim)
{
  if (((QTEL_HandlerTypeDef*)hsim)->key != QTEL_KEY)
    return QTEL_ERROR;

  sock->linkNum = -1;
  sock->socketManager = &((QTEL_HandlerTypeDef*)hsim)->socketManager;

  if (sock->config.autoReconnect) {
    Get_Available_LinkNum(sock->socketManager, &(sock->linkNum));
    if (sock->linkNum < 0) return QTEL_ERROR;
    sock->socketManager->sockets[sock->linkNum] = sock;
  }

  if (QTEL_SockManager_NetOpen(sock->socketManager) != QTEL_OK) return QTEL_ERROR;
  if (sock->socketManager->state != QTEL_SOCKMGR_STATE_NET_OPEN) {
    sock->state = SIM_SOCK_CLIENT_STATE_WAIT_NETOPEN;
    return QTEL_OK;
  }

  sockOpen(sock);

  return QTEL_OK;
}


QTEL_Status_t SIM_SockClient_Close(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *hsim = sock->socketManager->hsim;

  AT_Data_t paramData[4] = {
      AT_Number(sock->linkNum),
  };

  if (AT_Command(&hsim->atCmd, "+CIPCLOSE", 1, paramData, 0, 0) != AT_OK) {
    return QTEL_ERROR;
  }
  return QTEL_ERROR;
}


uint16_t SIM_SockClient_SendData(QTEL_SocketClient_t *sock, uint8_t *data, uint16_t length)
{
  QTEL_HandlerTypeDef *hsim = sock->socketManager->hsim;

  if (sock->state != SIM_SOCK_CLIENT_STATE_OPEN) return 0;

  AT_Data_t paramData[2] = {
      AT_Number(sock->linkNum),
      AT_Number(length),
  };

  if (AT_CommandWrite(&hsim->atCmd, "+CIPSEND", ">",
                      data, length,
                      2, paramData, 0, 0) != AT_OK)
  {
    return 0;
  }

  return length;
}


static QTEL_Status_t sockOpen(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *hsim = sock->socketManager->hsim;

  if (sock->linkNum == -1) {
    Get_Available_LinkNum(sock->socketManager, &(sock->linkNum));
    if (sock->linkNum < 0) return QTEL_ERROR;
    sock->socketManager->sockets[sock->linkNum] = sock;
  }

  AT_Data_t paramData[4] = {
      AT_Number(sock->linkNum),
      AT_String("TCP"),
      AT_String(sock->host),
      AT_Number(sock->port),
  };

  if (isSockConnected(sock)) {
    sockClose(sock);
    sock->state = SIM_SOCK_CLIENT_STATE_OPEN_PENDING;
    return QTEL_ERROR;
  }

  sock->state = SIM_SOCK_CLIENT_STATE_OPENING;
  sock->tick.connecting = hsim->getTick();
  if (AT_Command(&hsim->atCmd, "+CIPOPEN", 4, paramData, 0, 0) != AT_OK) {
    sock->state = SIM_SOCK_CLIENT_STATE_OPEN_PENDING;
    return QTEL_ERROR;
  }

  sock->listeners.onConnecting();

  return QTEL_OK;
}

static uint8_t isSockConnected(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *hsim = sock->socketManager->hsim;

  if (sock->linkNum < 0) return 0;
  AT_Data_t respData[10] = {
      AT_Number(0),
      AT_Number(0),
      AT_Number(0),
      AT_Number(0),
      AT_Number(0),
      AT_Number(0),
      AT_Number(0),
      AT_Number(0),
      AT_Number(0),
      AT_Number(0),
  };

  if (AT_Check(&hsim->atCmd, "+CIPCLOSE", 10, respData) == AT_OK) {
    if (respData[sock->linkNum].value.number == 1) return 1;
  }

  return 0;
}


static QTEL_Status_t sockClose(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *hsim = sock->socketManager->hsim;

  if (sock->linkNum < 0) return QTEL_ERROR;

  AT_Data_t paramData = AT_Number(sock->linkNum);

  if (AT_Command(&hsim->atCmd, "+CIPCLOSE", 1, &paramData, 0, 0) != AT_OK) {
    return QTEL_ERROR;
  }
  return QTEL_OK;
}


#endif /* QTEL_EN_FEATURE_SOCKET */
