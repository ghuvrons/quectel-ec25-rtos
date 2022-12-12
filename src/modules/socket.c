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


static QTEL_Status_t netOpen(QTEL_Socket_HandlerTypeDef *sockMgr);
static void onNetOpened(void *app, AT_Data_t*);
static void onSocketOpened(void *app, AT_Data_t*);
static void onSocketClosedByCmd(void *app, AT_Data_t*);
static void onSocketClosed(void *app, AT_Data_t*);
static struct AT_BufferReadTo onSocketReceived(void *app, AT_Data_t*);


QTEL_Status_t QTEL_SockManager_Init(QTEL_Socket_HandlerTypeDef *sockMgr, void *qtelPtr)
{
  if (((QTEL_HandlerTypeDef*)qtelPtr)->key != QTEL_KEY)
    return QTEL_ERROR;

  sockMgr->qtel = qtelPtr;
  sockMgr->state = QTEL_SOCKMGR_STATE_NET_CLOSE;
  sockMgr->stateTick = 0;

  AT_Data_t *netOpenResp = malloc(sizeof(AT_Data_t));
  AT_On(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "+NETOPEN",
        (QTEL_HandlerTypeDef*) qtelPtr, 1, netOpenResp, onNetOpened);


  AT_Data_t *socketOpenResp = malloc(sizeof(AT_Data_t)*2);
  AT_On(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "+CIPOPEN",
        (QTEL_HandlerTypeDef*) qtelPtr, 2, socketOpenResp, onSocketOpened);

  AT_Data_t *socketCloseResp = malloc(sizeof(AT_Data_t)*2);
  AT_On(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "+CIPCLOSE",
        (QTEL_HandlerTypeDef*) qtelPtr, 2, socketCloseResp, onSocketClosedByCmd);
  AT_On(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "+IPCLOSE",
        (QTEL_HandlerTypeDef*) qtelPtr, 2, socketCloseResp, onSocketClosed);

  AT_ReadIntoBufferOn(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "+RECEIVE",
                      (QTEL_HandlerTypeDef*) qtelPtr, 2, socketCloseResp, onSocketReceived);

  return QTEL_OK;
}

void QTEL_SockManager_SetState(QTEL_Socket_HandlerTypeDef *sockMgr, uint8_t newState)
{
  sockMgr->state = newState;
  ((QTEL_HandlerTypeDef*) sockMgr->qtel)->rtos.eventSet(QTEL_RTOS_EVT_SOCKMGR_NEW_STATE);
}


QTEL_Status_t QTEL_SockManager_OnNewState(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;

  sockMgr->stateTick = qtelPtr->getTick();

  switch (sockMgr->state) {
  case QTEL_SOCKMGR_STATE_NET_OPENING:
    netOpen(sockMgr);
    break;
  case QTEL_SOCKMGR_STATE_NET_OPEN:
    AT_Command(&qtelPtr->atCmd, "+CIPCCFG=10,0,0,1,1,0,10000", 0, 0, 0, 0);
    for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
      if (sockMgr->sockets[i] != 0)
        SIM_SockClient_OnNetOpened(sockMgr->sockets[i]);
    }
  }
  return QTEL_OK;
}

void QTEL_SockManager_CheckSocketsEvents(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
    if (sockMgr->sockets[i] != 0) {
      SIM_SockClient_CheckEvents(sockMgr->sockets[i]);
    }
  }
}

// this function will run every tick
void QTEL_SockManager_Loop(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;

  switch (sockMgr->state) {
  case QTEL_SOCKMGR_STATE_NET_CLOSE:
    break;

  case QTEL_SOCKMGR_STATE_NET_OPENING:
    if (QTEL_IsTimeout(qtelPtr, sockMgr->stateTick, 60000)) {
      QTEL_SockManager_SetState(&qtelPtr->socketManager, QTEL_SOCKMGR_STATE_NET_OPENING);
    }
    break;

  case QTEL_SOCKMGR_STATE_NET_OPEN_PENDING:
    if (QTEL_IsTimeout(qtelPtr, sockMgr->stateTick, 5000)) {
      QTEL_SockManager_SetState(&qtelPtr->socketManager, QTEL_SOCKMGR_STATE_NET_OPENING);
    }
    break;

  case QTEL_SOCKMGR_STATE_NET_OPEN:
    for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
      if (sockMgr->sockets[i] != 0) {
        SIM_SockClient_Loop(sockMgr->sockets[i]);
      }
    }
    break;

  default: break;
  }

  return;
}


QTEL_Status_t QTEL_SockManager_CheckNetOpen(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  AT_Data_t respData = AT_Number(0);
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;

  if (sockMgr->state == QTEL_SOCKMGR_STATE_NET_OPENING) return QTEL_OK;
  if (AT_Check(&qtelPtr->atCmd, "+NETOPEN", 1, &respData) != AT_OK) return QTEL_ERROR;
  if (respData.value.number == 1) {
    if (sockMgr->state != QTEL_SOCKMGR_STATE_NET_OPEN) {
      QTEL_SockManager_SetState(&qtelPtr->socketManager, QTEL_SOCKMGR_STATE_NET_OPEN);
    }
  }
  return QTEL_OK;
}


QTEL_Status_t QTEL_SockManager_NetOpen(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;

  if (sockMgr->state == QTEL_SOCKMGR_STATE_NET_OPENING) return QTEL_OK;

  if (QTEL_SockManager_CheckNetOpen(sockMgr) != QTEL_OK) return QTEL_ERROR;
  if (sockMgr->state == QTEL_SOCKMGR_STATE_NET_OPEN) return QTEL_OK;

  QTEL_SockManager_SetState(&qtelPtr->socketManager, QTEL_SOCKMGR_STATE_NET_OPENING);

  return QTEL_OK;
}


static QTEL_Status_t netOpen(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;

  if (AT_Command(&qtelPtr->atCmd, "+NETOPEN", 0, 0, 0, 0) != AT_OK) {
    QTEL_SockManager_SetState(&qtelPtr->socketManager, QTEL_SOCKMGR_STATE_NET_OPEN_PENDING);
    return QTEL_ERROR;
  }

  return QTEL_OK;
}


static void onNetOpened(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;

  if (resp->value.number == 0) {
    QTEL_SockManager_SetState(&qtelPtr->socketManager, QTEL_SOCKMGR_STATE_NET_OPEN);
  } else {
    QTEL_SockManager_SetState(&qtelPtr->socketManager, QTEL_SOCKMGR_STATE_NET_OPEN_PENDING);
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
