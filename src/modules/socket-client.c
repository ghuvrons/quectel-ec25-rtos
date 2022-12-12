/*
 * socket-client.c
 *
 *  Created on: Nov 10, 2022
 *      Author: janoko
 */

#include "../include/simcom/socket-client.h"
#if SIM_EN_FEATURE_SOCKET

#include "../include/simcom.h"
#include "../include/simcom/socket.h"
#include "../include/simcom/utils.h"
#include <string.h>


#define Get_Available_LinkNum(hsimsock, linkNum) {\
  for (int16_t i = 0; i < SIM_NUM_OF_SOCKET; i++) {\
    if ((hsimsock)->sockets[i] == NULL) {\
      *(linkNum) = i;\
      break;\
    }\
  }\
}


static SIM_Status_t sockOpen(SIM_SocketClient_t *sock);
static uint8_t isSockConnected(SIM_SocketClient_t *sock);
static SIM_Status_t sockClose(SIM_SocketClient_t *sock);


SIM_Status_t SIM_SockClient_Init(SIM_SocketClient_t *sock, const char *host, uint16_t port, void *buffer)
{
  char *sockIP = sock->host;
  while (*host != '\0') {
    *sockIP = *host;
    host++;
    sockIP++;
  }

  sock->port = port;

  if (sock->config.timeout == 0)
    sock->config.timeout = SIM_SOCK_DEFAULT_TO;
  if (sock->config.reconnectingDelay == 0)
    sock->config.reconnectingDelay = 5000;

  sock->linkNum = -1;
  sock->buffer = buffer;
  if (sock->buffer == NULL)
    return SIM_ERROR;

  sock->state = SIM_SOCK_CLIENT_STATE_CLOSE;
  return SIM_OK;
}


SIM_Status_t SIM_SockClient_OnNetOpened(SIM_SocketClient_t *sock)
{
  if (sock->state == SIM_SOCK_CLIENT_STATE_WAIT_NETOPEN) {
    return sockOpen(sock);
  }
  return SIM_OK;
}


SIM_Status_t SIM_SockClient_CheckEvents(SIM_SocketClient_t *sock)
{
  SIM_HandlerTypeDef *hsim = sock->socketManager->hsim;

  if (SIM_BITS_IS(sock->events, SIM_SOCK_EVENT_ON_OPENED)) {
    SIM_BITS_UNSET(sock->events, SIM_SOCK_EVENT_ON_OPENED);
    if (sock->listeners.onConnected) sock->listeners.onConnected();
  }
  if (SIM_BITS_IS(sock->events, SIM_SOCK_EVENT_ON_CLOSED)) {
    SIM_BITS_UNSET(sock->events, SIM_SOCK_EVENT_ON_CLOSED);
    if (sock->state == SIM_SOCK_CLIENT_STATE_OPEN_PENDING) {
      sockOpen(sock);
    } else {
      sock->state = SIM_SOCK_CLIENT_STATE_CLOSE;
      sock->tick.reconnDelay = hsim->getTick();
      if (sock->listeners.onClosed) sock->listeners.onClosed();
    }
  }
  return SIM_OK;
}


SIM_Status_t SIM_SockClient_Loop(SIM_SocketClient_t *sock)
{
  SIM_HandlerTypeDef *hsim = sock->socketManager->hsim;

  switch (sock->state) {
  case SIM_SOCK_CLIENT_STATE_WAIT_NETOPEN:
    sockOpen(sock);
    break;

  case SIM_SOCK_CLIENT_STATE_OPENING:
    if (sock->tick.connecting && SIM_IsTimeout(hsim, sock->tick.connecting, 30000)) {
      sock->state = SIM_SOCK_CLIENT_STATE_OPEN_PENDING;
      SIM_SockClient_Close(sock);
    }
    break;

  case SIM_SOCK_CLIENT_STATE_CLOSE:
    if (sock->tick.reconnDelay == 0) {

    }
    else if (SIM_IsTimeout(hsim, sock->tick.reconnDelay, 2000)) {
      sockOpen(sock);
    }
    break;

  default: break;
  }

  return SIM_OK;
}


void SIM_SockClient_SetBuffer(SIM_SocketClient_t *sock, void *buffer)
{
  sock->buffer = buffer;
}


SIM_Status_t SIM_SockClient_Open(SIM_SocketClient_t *sock, void *hsim)
{
  if (((SIM_HandlerTypeDef*)hsim)->key != SIM_KEY)
    return SIM_ERROR;

  sock->linkNum = -1;
  sock->socketManager = &((SIM_HandlerTypeDef*)hsim)->socketManager;

  if (sock->config.autoReconnect) {
    Get_Available_LinkNum(sock->socketManager, &(sock->linkNum));
    if (sock->linkNum < 0) return SIM_ERROR;
    sock->socketManager->sockets[sock->linkNum] = sock;
  }

  if (SIM_SockManager_NetOpen(sock->socketManager) != SIM_OK) return SIM_ERROR;
  if (sock->socketManager->state != SIM_SOCKMGR_STATE_NET_OPEN) {
    sock->state = SIM_SOCK_CLIENT_STATE_WAIT_NETOPEN;
    return SIM_OK;
  }

  sockOpen(sock);

  return SIM_OK;
}


SIM_Status_t SIM_SockClient_Close(SIM_SocketClient_t *sock)
{
  SIM_HandlerTypeDef *hsim = sock->socketManager->hsim;

  AT_Data_t paramData[4] = {
      AT_Number(sock->linkNum),
  };

  if (AT_Command(&hsim->atCmd, "+CIPCLOSE", 1, paramData, 0, 0) != AT_OK) {
    return SIM_ERROR;
  }
  return SIM_ERROR;
}


uint16_t SIM_SockClient_SendData(SIM_SocketClient_t *sock, uint8_t *data, uint16_t length)
{
  SIM_HandlerTypeDef *hsim = sock->socketManager->hsim;

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


static SIM_Status_t sockOpen(SIM_SocketClient_t *sock)
{
  SIM_HandlerTypeDef *hsim = sock->socketManager->hsim;

  if (sock->linkNum == -1) {
    Get_Available_LinkNum(sock->socketManager, &(sock->linkNum));
    if (sock->linkNum < 0) return SIM_ERROR;
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
    return SIM_ERROR;
  }

  sock->state = SIM_SOCK_CLIENT_STATE_OPENING;
  sock->tick.connecting = hsim->getTick();
  if (AT_Command(&hsim->atCmd, "+CIPOPEN", 4, paramData, 0, 0) != AT_OK) {
    sock->state = SIM_SOCK_CLIENT_STATE_OPEN_PENDING;
    return SIM_ERROR;
  }

  sock->listeners.onConnecting();

  return SIM_OK;
}

static uint8_t isSockConnected(SIM_SocketClient_t *sock)
{
  SIM_HandlerTypeDef *hsim = sock->socketManager->hsim;

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


static SIM_Status_t sockClose(SIM_SocketClient_t *sock)
{
  SIM_HandlerTypeDef *hsim = sock->socketManager->hsim;

  if (sock->linkNum < 0) return SIM_ERROR;

  AT_Data_t paramData = AT_Number(sock->linkNum);

  if (AT_Command(&hsim->atCmd, "+CIPCLOSE", 1, &paramData, 0, 0) != AT_OK) {
    return SIM_ERROR;
  }
  return SIM_OK;
}


#endif /* SIM_EN_FEATURE_SOCKET */
