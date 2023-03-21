/*
 * socket.c
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#include <quectel-ec25/socket.h>
#if QTEL_EN_FEATURE_SOCKET

#include <quectel-ec25.h>
#include <quectel-ec25/net.h>
#include <quectel-ec25/socket-client.h>
#include <quectel-ec25/utils.h>
#include "../events.h"
#include <stdlib.h>


static void onSocketOpened(void *app, AT_Data_t*);
static void onSocketEvent(void *app, AT_Data_t*);

QTEL_Status_t QTEL_SockManager_Init(QTEL_Socket_HandlerTypeDef *sockMgr, void *qtelPtr)
{
  if (((QTEL_HandlerTypeDef*)qtelPtr)->key != QTEL_KEY)
    return QTEL_ERROR;

  sockMgr->qtel = qtelPtr;
  sockMgr->state = QTEL_SOCKH_STATE_NON_ACTIVE;
  sockMgr->contextId = QTEL_CID_SOCKET;

  AT_Data_t *socketOpenResp = malloc(sizeof(AT_Data_t)*2);
  AT_On(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "+QIOPEN",
        (QTEL_HandlerTypeDef*) qtelPtr, 2, socketOpenResp, onSocketOpened);

  uint8_t *sockEvtStr = malloc(16);
  AT_Data_t *socketEventResp = malloc(sizeof(AT_Data_t)*2);
  AT_DataSetBuffer(socketEventResp, sockEvtStr, 16);
  AT_On(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "+QIURC",
        (QTEL_HandlerTypeDef*) qtelPtr, 2, socketEventResp, onSocketEvent);

  return QTEL_OK;
}


void QTEL_SockManager_OnQTELActive(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  if (sockMgr->state == QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING) {
    QTEL_SockManager_SetState(sockMgr, QTEL_SOCKH_STATE_PDP_ACTIVATING);
  }
}


void QTEL_SockManager_SetState(QTEL_Socket_HandlerTypeDef *sockMgr, uint8_t newState)
{
  sockMgr->state = newState;
  ((QTEL_HandlerTypeDef*) sockMgr->qtel)->rtos.eventSet(QTEL_RTOS_EVT_SOCKH_NEW_EVT);
}


void QTEL_SockManager_OnNewState(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;

  switch (sockMgr->state) {
  case QTEL_SOCKH_STATE_PDP_ACTIVATING:
    if (qtelPtr->net.APN.APN != NULL) {
      if (QTEL_NET_ConfigureContext(&qtelPtr->net, sockMgr->contextId) == QTEL_OK)
      {
        QTEL_Debug("Socket PDP Context was configured");
      }
      if (QTEL_NET_ActivateContext(&qtelPtr->net, sockMgr->contextId) == QTEL_OK)
      {
        QTEL_Debug("Socket PDP Context active");
        QTEL_SockManager_SetState(sockMgr, QTEL_SOCKH_STATE_PDP_ACTIVE);
      }
    }
    break;

  case QTEL_SOCKH_STATE_PDP_ACTIVE:
    for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
      if (sockMgr->sockets[i] != 0)
        QTEL_SockClient_OnNetOpened(sockMgr->sockets[i]);
    }
    break;
  }
}


void QTEL_SockManager_CheckSocketsEvents(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
    if (sockMgr->sockets[i] != 0) {
      QTEL_SockClient_CheckEvents(sockMgr->sockets[i]);
    }
  }
}

void QTEL_SockManager_PDP_Activate(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;

  if (qtelPtr->state != QTEL_STATE_ACTIVE) {
    QTEL_SockManager_SetState(sockMgr, QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING);
  }
  else if (sockMgr->state != QTEL_SOCKH_STATE_PDP_ACTIVATING) {
    QTEL_SockManager_SetState(sockMgr, QTEL_SOCKH_STATE_PDP_ACTIVATING);
  }
}

// this function will run every tick
void QTEL_SockManager_Loop(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;

  if (qtelPtr->net.state == QTEL_NET_STATE_ONLINE) {
    for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++)
    {
      if (sockMgr->sockets[i] != 0) {
        QTEL_SockClient_Loop(sockMgr->sockets[i]);
      }
    }
  }

  return;
}


static void onSocketOpened(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;
  uint8_t linkNum = resp->value.number;

  resp++;
  uint8_t err = resp->value.number;

  QTEL_SocketClient_t *sock = qtelPtr->socketManager.sockets[linkNum];
  if (sock != 0) {
    if (err == 0) {
      sock->state = QTEL_SOCK_STATE_OPEN;
      QTEL_BITS_SET(sock->events, QTEL_SOCK_EVENT_ON_OPENED);
      qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
    }
    else {
      sock->state = QTEL_SOCK_STATE_CLOSE;
      QTEL_BITS_SET(sock->events, QTEL_SOCK_EVENT_ON_OPENING_ERROR);
      qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
    }
  }
}


static void onSocketEvent(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;

  const char *evt = resp->value.string;
  resp++;
  uint8_t linkNum = resp->value.number;
  QTEL_SocketClient_t *sock = qtelPtr->socketManager.sockets[linkNum];

  if (strncmp(evt, "recv", 4) == 0) {
    QTEL_BITS_SET(sock->events, QTEL_SOCK_EVENT_ON_RECV_DATA_AVAILABLE);
    qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
  }
  else if (strncmp(evt, "closed", 6) == 0) {
    sock->state = QTEL_SOCK_STATE_CLOSE;
    QTEL_BITS_SET(sock->events, QTEL_SOCK_EVENT_ON_CLOSED);
    qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
  }
}


#endif /* QTEL_EN_FEATURE_SOCKET */
