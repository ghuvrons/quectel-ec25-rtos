/*
 * socket.c
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#include <quectel-ec25/socket.h>
#if QTEL_EN_FEATURE_SOCKET

#include "../include/quectel-ec25.h"
#include <quectel-ec25/net.h>
#include <quectel-ec25/utils.h>
#include "../events.h"
#include <stdlib.h>


static QTEL_Status_t netOpen(QTEL_Socket_HandlerTypeDef *hsimSockMgr);
static void onNetOpened(void *app, AT_Data_t*);
static void onSocketOpened(void *app, AT_Data_t*);
static void onSocketClosedByCmd(void *app, AT_Data_t*);
static void onSocketClosed(void *app, AT_Data_t*);
static struct AT_BufferReadTo onSocketReceived(void *app, AT_Data_t*);


QTEL_Status_t QTEL_SockManager_Init(QTEL_Socket_HandlerTypeDef *hsimSockMgr, void *hsim)
{
  if (((QTEL_HandlerTypeDef*)hsim)->key != QTEL_KEY)
    return QTEL_ERROR;

  hsimSockMgr->hsim = hsim;
  hsimSockMgr->state = QTEL_SOCKMGR_STATE_NET_CLOSE;
  hsimSockMgr->stateTick = 0;

  AT_Data_t *netOpenResp = malloc(sizeof(AT_Data_t));
  AT_On(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+NETOPEN",
        (QTEL_HandlerTypeDef*) hsim, 1, netOpenResp, onNetOpened);


  AT_Data_t *socketOpenResp = malloc(sizeof(AT_Data_t)*2);
  AT_On(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+CIPOPEN",
        (QTEL_HandlerTypeDef*) hsim, 2, socketOpenResp, onSocketOpened);

  AT_Data_t *socketCloseResp = malloc(sizeof(AT_Data_t)*2);
  AT_On(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+CIPCLOSE",
        (QTEL_HandlerTypeDef*) hsim, 2, socketCloseResp, onSocketClosedByCmd);
  AT_On(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+IPCLOSE",
        (QTEL_HandlerTypeDef*) hsim, 2, socketCloseResp, onSocketClosed);

  AT_ReadIntoBufferOn(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+RECEIVE",
                      (QTEL_HandlerTypeDef*) hsim, 2, socketCloseResp, onSocketReceived);

  return QTEL_OK;
}

void QTEL_SockManager_SetState(QTEL_Socket_HandlerTypeDef *hsimSockMgr, uint8_t newState)
{
  hsimSockMgr->state = newState;
  ((QTEL_HandlerTypeDef*) hsimSockMgr->hsim)->rtos.eventSet(QTEL_RTOS_EVT_SOCKMGR_NEW_STATE);
}


QTEL_Status_t QTEL_SockManager_OnNewState(QTEL_Socket_HandlerTypeDef *hsimSockMgr)
{
  QTEL_HandlerTypeDef *hsim = hsimSockMgr->hsim;

  hsimSockMgr->stateTick = hsim->getTick();

  switch (hsimSockMgr->state) {
  case QTEL_SOCKMGR_STATE_NET_OPENING:
    netOpen(hsimSockMgr);
    break;
  case QTEL_SOCKMGR_STATE_NET_OPEN:
    AT_Command(&hsim->atCmd, "+CIPCCFG=10,0,0,1,1,0,10000", 0, 0, 0, 0);
    for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
      if (hsimSockMgr->sockets[i] != 0)
        SIM_SockClient_OnNetOpened(hsimSockMgr->sockets[i]);
    }
  }
  return QTEL_OK;
}

void QTEL_SockManager_CheckSocketsEvents(QTEL_Socket_HandlerTypeDef *hsimSockMgr)
{
  for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
    if (hsimSockMgr->sockets[i] != 0) {
      SIM_SockClient_CheckEvents(hsimSockMgr->sockets[i]);
    }
  }
}

// this function will run every tick
void QTEL_SockManager_Loop(QTEL_Socket_HandlerTypeDef *hsimSockMgr)
{
  QTEL_HandlerTypeDef *hsim = hsimSockMgr->hsim;

  switch (hsimSockMgr->state) {
  case QTEL_SOCKMGR_STATE_NET_CLOSE:
    break;

  case QTEL_SOCKMGR_STATE_NET_OPENING:
    if (QTEL_IsTimeout(hsim, hsimSockMgr->stateTick, 60000)) {
      QTEL_SockManager_SetState(&hsim->socketManager, QTEL_SOCKMGR_STATE_NET_OPENING);
    }
    break;

  case QTEL_SOCKMGR_STATE_NET_OPEN_PENDING:
    if (QTEL_IsTimeout(hsim, hsimSockMgr->stateTick, 5000)) {
      QTEL_SockManager_SetState(&hsim->socketManager, QTEL_SOCKMGR_STATE_NET_OPENING);
    }
    break;

  case QTEL_SOCKMGR_STATE_NET_OPEN:
    for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
      if (hsimSockMgr->sockets[i] != 0) {
        SIM_SockClient_Loop(hsimSockMgr->sockets[i]);
      }
    }
    break;

  default: break;
  }

  return;
}


QTEL_Status_t QTEL_SockManager_CheckNetOpen(QTEL_Socket_HandlerTypeDef *hsimSockMgr)
{
  AT_Data_t respData = AT_Number(0);
  QTEL_HandlerTypeDef *hsim = hsimSockMgr->hsim;

  if (hsimSockMgr->state == QTEL_SOCKMGR_STATE_NET_OPENING) return QTEL_OK;
  if (AT_Check(&hsim->atCmd, "+NETOPEN", 1, &respData) != AT_OK) return QTEL_ERROR;
  if (respData.value.number == 1) {
    if (hsimSockMgr->state != QTEL_SOCKMGR_STATE_NET_OPEN) {
      QTEL_SockManager_SetState(&hsim->socketManager, QTEL_SOCKMGR_STATE_NET_OPEN);
    }
  }
  return QTEL_OK;
}


QTEL_Status_t QTEL_SockManager_NetOpen(QTEL_Socket_HandlerTypeDef *hsimSockMgr)
{
  QTEL_HandlerTypeDef *hsim = hsimSockMgr->hsim;

  if (hsimSockMgr->state == QTEL_SOCKMGR_STATE_NET_OPENING) return QTEL_OK;

  if (QTEL_SockManager_CheckNetOpen(hsimSockMgr) != QTEL_OK) return QTEL_ERROR;
  if (hsimSockMgr->state == QTEL_SOCKMGR_STATE_NET_OPEN) return QTEL_OK;

  QTEL_SockManager_SetState(&hsim->socketManager, QTEL_SOCKMGR_STATE_NET_OPENING);

  return QTEL_OK;
}


static QTEL_Status_t netOpen(QTEL_Socket_HandlerTypeDef *hsimSockMgr)
{
  QTEL_HandlerTypeDef *hsim = hsimSockMgr->hsim;

  if (AT_Command(&hsim->atCmd, "+NETOPEN", 0, 0, 0, 0) != AT_OK) {
    QTEL_SockManager_SetState(&hsim->socketManager, QTEL_SOCKMGR_STATE_NET_OPEN_PENDING);
    return QTEL_ERROR;
  }

  return QTEL_OK;
}


static void onNetOpened(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;

  if (resp->value.number == 0) {
    QTEL_SockManager_SetState(&hsim->socketManager, QTEL_SOCKMGR_STATE_NET_OPEN);
  } else {
    QTEL_SockManager_SetState(&hsim->socketManager, QTEL_SOCKMGR_STATE_NET_OPEN_PENDING);
  }
}

static void onSocketOpened(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;
  uint8_t linkNum = resp->value.number;

  resp++;
  uint8_t err = resp->value.number;

  QTEL_SocketClient_t *sock = hsim->socketManager.sockets[linkNum];
  if (sock != 0) {
    if (err == 0) {
      sock->state = SIM_SOCK_CLIENT_STATE_OPEN;
      QTEL_BITS_SET(sock->events, SIM_SOCK_EVENT_ON_OPENED);
      hsim->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
    }
  }
}


static void onSocketClosedByCmd(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;
  uint8_t linkNum = resp->value.number;

  resp++;
  uint8_t err = resp->value.number;

  QTEL_SocketClient_t *sock = hsim->socketManager.sockets[linkNum];
  if (sock != 0) {
    if (err == 0) {
      QTEL_BITS_SET(sock->events, SIM_SOCK_EVENT_ON_CLOSED);
      hsim->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
    }
  }
}


static void onSocketClosed(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;
  uint8_t linkNum = resp->value.number;

  resp++;

  QTEL_SocketClient_t *sock = hsim->socketManager.sockets[linkNum];
  if (sock != 0) {
    QTEL_BITS_SET(sock->events, SIM_SOCK_EVENT_ON_CLOSED);
    hsim->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
  }
}


static struct AT_BufferReadTo onSocketReceived(void *app, AT_Data_t *resp)
{
  struct AT_BufferReadTo returnBuf = {
      .buffer = 0, .bufferSize = 0, .readLen = 0,
  };
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;
  uint8_t linkNum = resp->value.number;

  resp++;
  uint8_t length = resp->value.number;

  QTEL_SocketClient_t *sock = hsim->socketManager.sockets[linkNum];
  if (sock != 0) {
    returnBuf.buffer = sock->buffer;
  }
  returnBuf.bufferSize = length;
  returnBuf.readLen = length;
  return returnBuf;
}


#endif /* QTEL_EN_FEATURE_SOCKET */
