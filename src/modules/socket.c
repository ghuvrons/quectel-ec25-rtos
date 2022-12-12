/*
 * socket.c
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#include "../include/simcom/socket.h"
#if SIM_EN_FEATURE_SOCKET

#include "../include/simcom.h"
#include "../include/simcom/net.h"
#include "../include/simcom/utils.h"
#include "../events.h"
#include <stdlib.h>


static SIM_Status_t netOpen(SIM_Socket_HandlerTypeDef *hsimSockMgr);
static void onNetOpened(void *app, AT_Data_t*);
static void onSocketOpened(void *app, AT_Data_t*);
static void onSocketClosedByCmd(void *app, AT_Data_t*);
static void onSocketClosed(void *app, AT_Data_t*);
static struct AT_BufferReadTo onSocketReceived(void *app, AT_Data_t*);


SIM_Status_t SIM_SockManager_Init(SIM_Socket_HandlerTypeDef *hsimSockMgr, void *hsim)
{
  if (((SIM_HandlerTypeDef*)hsim)->key != SIM_KEY)
    return SIM_ERROR;

  hsimSockMgr->hsim = hsim;
  hsimSockMgr->state = SIM_SOCKMGR_STATE_NET_CLOSE;
  hsimSockMgr->stateTick = 0;

  AT_Data_t *netOpenResp = malloc(sizeof(AT_Data_t));
  AT_On(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+NETOPEN",
        (SIM_HandlerTypeDef*) hsim, 1, netOpenResp, onNetOpened);


  AT_Data_t *socketOpenResp = malloc(sizeof(AT_Data_t)*2);
  AT_On(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+CIPOPEN",
        (SIM_HandlerTypeDef*) hsim, 2, socketOpenResp, onSocketOpened);

  AT_Data_t *socketCloseResp = malloc(sizeof(AT_Data_t)*2);
  AT_On(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+CIPCLOSE",
        (SIM_HandlerTypeDef*) hsim, 2, socketCloseResp, onSocketClosedByCmd);
  AT_On(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+IPCLOSE",
        (SIM_HandlerTypeDef*) hsim, 2, socketCloseResp, onSocketClosed);

  AT_ReadIntoBufferOn(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+RECEIVE",
                      (SIM_HandlerTypeDef*) hsim, 2, socketCloseResp, onSocketReceived);

  return SIM_OK;
}

void SIM_SockManager_SetState(SIM_Socket_HandlerTypeDef *hsimSockMgr, uint8_t newState)
{
  hsimSockMgr->state = newState;
  ((SIM_HandlerTypeDef*) hsimSockMgr->hsim)->rtos.eventSet(SIM_RTOS_EVT_SOCKMGR_NEW_STATE);
}


SIM_Status_t SIM_SockManager_OnNewState(SIM_Socket_HandlerTypeDef *hsimSockMgr)
{
  SIM_HandlerTypeDef *hsim = hsimSockMgr->hsim;

  hsimSockMgr->stateTick = hsim->getTick();

  switch (hsimSockMgr->state) {
  case SIM_SOCKMGR_STATE_NET_OPENING:
    netOpen(hsimSockMgr);
    break;
  case SIM_SOCKMGR_STATE_NET_OPEN:
    AT_Command(&hsim->atCmd, "+CIPCCFG=10,0,0,1,1,0,10000", 0, 0, 0, 0);
    for (uint8_t i = 0; i < SIM_NUM_OF_SOCKET; i++) {
      if (hsimSockMgr->sockets[i] != 0)
        SIM_SockClient_OnNetOpened(hsimSockMgr->sockets[i]);
    }
  }
  return SIM_OK;
}

void SIM_SockManager_CheckSocketsEvents(SIM_Socket_HandlerTypeDef *hsimSockMgr)
{
  for (uint8_t i = 0; i < SIM_NUM_OF_SOCKET; i++) {
    if (hsimSockMgr->sockets[i] != 0) {
      SIM_SockClient_CheckEvents(hsimSockMgr->sockets[i]);
    }
  }
}

// this function will run every tick
void SIM_SockManager_Loop(SIM_Socket_HandlerTypeDef *hsimSockMgr)
{
  SIM_HandlerTypeDef *hsim = hsimSockMgr->hsim;

  switch (hsimSockMgr->state) {
  case SIM_SOCKMGR_STATE_NET_CLOSE:
    break;

  case SIM_SOCKMGR_STATE_NET_OPENING:
    if (SIM_IsTimeout(hsim, hsimSockMgr->stateTick, 60000)) {
      SIM_SockManager_SetState(&hsim->socketManager, SIM_SOCKMGR_STATE_NET_OPENING);
    }
    break;

  case SIM_SOCKMGR_STATE_NET_OPEN_PENDING:
    if (SIM_IsTimeout(hsim, hsimSockMgr->stateTick, 5000)) {
      SIM_SockManager_SetState(&hsim->socketManager, SIM_SOCKMGR_STATE_NET_OPENING);
    }
    break;

  case SIM_SOCKMGR_STATE_NET_OPEN:
    for (uint8_t i = 0; i < SIM_NUM_OF_SOCKET; i++) {
      if (hsimSockMgr->sockets[i] != 0) {
        SIM_SockClient_Loop(hsimSockMgr->sockets[i]);
      }
    }
    break;

  default: break;
  }

  return;
}


SIM_Status_t SIM_SockManager_CheckNetOpen(SIM_Socket_HandlerTypeDef *hsimSockMgr)
{
  AT_Data_t respData = AT_Number(0);
  SIM_HandlerTypeDef *hsim = hsimSockMgr->hsim;

  if (hsimSockMgr->state == SIM_SOCKMGR_STATE_NET_OPENING) return SIM_OK;
  if (AT_Check(&hsim->atCmd, "+NETOPEN", 1, &respData) != AT_OK) return SIM_ERROR;
  if (respData.value.number == 1) {
    if (hsimSockMgr->state != SIM_SOCKMGR_STATE_NET_OPEN) {
      SIM_SockManager_SetState(&hsim->socketManager, SIM_SOCKMGR_STATE_NET_OPEN);
    }
  }
  return SIM_OK;
}


SIM_Status_t SIM_SockManager_NetOpen(SIM_Socket_HandlerTypeDef *hsimSockMgr)
{
  SIM_HandlerTypeDef *hsim = hsimSockMgr->hsim;

  if (hsimSockMgr->state == SIM_SOCKMGR_STATE_NET_OPENING) return SIM_OK;

  if (SIM_SockManager_CheckNetOpen(hsimSockMgr) != SIM_OK) return SIM_ERROR;
  if (hsimSockMgr->state == SIM_SOCKMGR_STATE_NET_OPEN) return SIM_OK;

  SIM_SockManager_SetState(&hsim->socketManager, SIM_SOCKMGR_STATE_NET_OPENING);

  return SIM_OK;
}


static SIM_Status_t netOpen(SIM_Socket_HandlerTypeDef *hsimSockMgr)
{
  SIM_HandlerTypeDef *hsim = hsimSockMgr->hsim;

  if (AT_Command(&hsim->atCmd, "+NETOPEN", 0, 0, 0, 0) != AT_OK) {
    SIM_SockManager_SetState(&hsim->socketManager, SIM_SOCKMGR_STATE_NET_OPEN_PENDING);
    return SIM_ERROR;
  }

  return SIM_OK;
}


static void onNetOpened(void *app, AT_Data_t *resp)
{
  SIM_HandlerTypeDef *hsim = (SIM_HandlerTypeDef*)app;

  if (resp->value.number == 0) {
    SIM_SockManager_SetState(&hsim->socketManager, SIM_SOCKMGR_STATE_NET_OPEN);
  } else {
    SIM_SockManager_SetState(&hsim->socketManager, SIM_SOCKMGR_STATE_NET_OPEN_PENDING);
  }
}

static void onSocketOpened(void *app, AT_Data_t *resp)
{
  SIM_HandlerTypeDef *hsim = (SIM_HandlerTypeDef*)app;
  uint8_t linkNum = resp->value.number;

  resp++;
  uint8_t err = resp->value.number;

  SIM_SocketClient_t *sock = hsim->socketManager.sockets[linkNum];
  if (sock != 0) {
    if (err == 0) {
      sock->state = SIM_SOCK_CLIENT_STATE_OPEN;
      SIM_BITS_SET(sock->events, SIM_SOCK_EVENT_ON_OPENED);
      hsim->rtos.eventSet(SIM_RTOS_EVT_SOCKCLIENT_NEW_EVT);
    }
  }
}


static void onSocketClosedByCmd(void *app, AT_Data_t *resp)
{
  SIM_HandlerTypeDef *hsim = (SIM_HandlerTypeDef*)app;
  uint8_t linkNum = resp->value.number;

  resp++;
  uint8_t err = resp->value.number;

  SIM_SocketClient_t *sock = hsim->socketManager.sockets[linkNum];
  if (sock != 0) {
    if (err == 0) {
      SIM_BITS_SET(sock->events, SIM_SOCK_EVENT_ON_CLOSED);
      hsim->rtos.eventSet(SIM_RTOS_EVT_SOCKCLIENT_NEW_EVT);
    }
  }
}


static void onSocketClosed(void *app, AT_Data_t *resp)
{
  SIM_HandlerTypeDef *hsim = (SIM_HandlerTypeDef*)app;
  uint8_t linkNum = resp->value.number;

  resp++;

  SIM_SocketClient_t *sock = hsim->socketManager.sockets[linkNum];
  if (sock != 0) {
    SIM_BITS_SET(sock->events, SIM_SOCK_EVENT_ON_CLOSED);
    hsim->rtos.eventSet(SIM_RTOS_EVT_SOCKCLIENT_NEW_EVT);
  }
}


static struct AT_BufferReadTo onSocketReceived(void *app, AT_Data_t *resp)
{
  struct AT_BufferReadTo returnBuf = {
      .buffer = 0, .bufferSize = 0, .readLen = 0,
  };
  SIM_HandlerTypeDef *hsim = (SIM_HandlerTypeDef*)app;
  uint8_t linkNum = resp->value.number;

  resp++;
  uint8_t length = resp->value.number;

  SIM_SocketClient_t *sock = hsim->socketManager.sockets[linkNum];
  if (sock != 0) {
    returnBuf.buffer = sock->buffer;
  }
  returnBuf.bufferSize = length;
  returnBuf.readLen = length;
  return returnBuf;
}


#endif /* SIM_EN_FEATURE_SOCKET */
