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
  sockMgr->sslcontextId = QTEL_SSLID_SOCKET;
  sockMgr->activatingTick = 0;
  sockMgr->activatingPendingTick = 0;

  AT_Data_t *socketOpenResp = malloc(sizeof(AT_Data_t)*2);
  memset(socketOpenResp, 0, sizeof(AT_Data_t)*2);
  AT_On(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "+QIOPEN",
        (QTEL_HandlerTypeDef*) qtelPtr, 2, socketOpenResp, onSocketOpened);

  AT_Data_t *socketOpenRespSSL = malloc(sizeof(AT_Data_t)*2);
  memset(socketOpenRespSSL, 0, sizeof(AT_Data_t) * 2);
  AT_On(&((QTEL_HandlerTypeDef *)qtelPtr)->atCmd, "+QSSLOPEN",
        (QTEL_HandlerTypeDef *)qtelPtr, 2, socketOpenRespSSL, onSocketOpened);

  uint8_t *sockEvtStr = malloc(16);
  AT_Data_t *socketEventResp = malloc(sizeof(AT_Data_t)*2);
  memset(socketEventResp, 0, sizeof(AT_Data_t)*2);
  AT_DataSetBuffer(socketEventResp, sockEvtStr, 16);
  AT_On(&((QTEL_HandlerTypeDef*)qtelPtr)->atCmd, "+QIURC",
        (QTEL_HandlerTypeDef*) qtelPtr, 2, socketEventResp, onSocketEvent);

  uint8_t *sockEvtStrSSL = malloc(16);
  AT_Data_t *socketEventRespSSL = malloc(sizeof(AT_Data_t)*2);
  memset(socketEventRespSSL, 0, sizeof(AT_Data_t) * 2);
  AT_DataSetBuffer(socketEventRespSSL, sockEvtStrSSL, 16);
  AT_On(&((QTEL_HandlerTypeDef *)qtelPtr)->atCmd, "+QSSLURC",
        (QTEL_HandlerTypeDef *)qtelPtr, 2, socketEventRespSSL, onSocketEvent);

  return QTEL_OK;
}


void QTEL_SockManager_SetState(QTEL_Socket_HandlerTypeDef *sockMgr, uint8_t newState)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;

  if (newState == QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING) sockMgr->activatingPendingTick = qtelPtr->getTick();
  else if (newState == QTEL_SOCKH_STATE_PDP_ACTIVATING)    sockMgr->activatingTick = qtelPtr->getTick();
  sockMgr->state = newState;
  qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_SOCKH_NEW_EVT);
}


void QTEL_SockManager_OnNewState(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  switch (sockMgr->state) {
  case QTEL_SOCKH_STATE_PDP_ACTIVATING:
    QTEL_SockManager_PDP_Activate(sockMgr);
    break;

  case QTEL_SOCKH_STATE_PDP_ACTIVE:
    for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
      if (sockMgr->sockets[i] != NULL)
        QTEL_SockClient_OnNetOpened(sockMgr->sockets[i]);
    }
    break;

  default: break;
  }
}


void QTEL_SockManager_OnPoweredDown(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  if (sockMgr->state > QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING) {
    QTEL_SockManager_SetState(sockMgr, QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING);
  }

  for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
    if (sockMgr->sockets[i] != NULL) {
      QTEL_SockClient_OnPoweredDown(sockMgr->sockets[i]);
    }
  }
}

void QTEL_SockManager_CheckSocketsEvents(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  for (uint8_t i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
    if (sockMgr->sockets[i] != NULL) {
      QTEL_SockClient_CheckEvents(sockMgr->sockets[i]);
    }
  }
}

QTEL_Status_t QTEL_SockManager_PDP_IsActivate(QTEL_Socket_HandlerTypeDef *sockMgr, uint8_t *isActive)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;
  QTEL_Status_t status;

  if (qtelPtr->state < QTEL_STATE_ACTIVE
      || qtelPtr->net.state != QTEL_NET_STATE_ACTIVE
      || !(QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED)
           || QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_LTE_REGISTERED)))
  {
    *isActive = 0;
    return QTEL_OK;
  }

  if (QTEL_IS_STATUS(&qtelPtr->net, QTEL_NET_PDP_ACTIVATING)) {
    *isActive = 0;
    return QTEL_OK;
  }

  return QTEL_NET_IsPDPActive(&qtelPtr->net, sockMgr->contextId, isActive);
}

QTEL_Status_t QTEL_SockManager_PDP_Activate(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;
  QTEL_Status_t status;

  if (qtelPtr->state < QTEL_STATE_ACTIVE 
      || qtelPtr->net.state != QTEL_NET_STATE_ACTIVE
      || !(QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED) 
           || QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_LTE_REGISTERED)))
  {
    QTEL_SockManager_SetState(sockMgr, QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING);
    return QTEL_ERROR_PENDING;
  }

  sockMgr->activatingTick = qtelPtr->getTick();
  status = QTEL_NET_ActivatePDP(&qtelPtr->net, sockMgr->contextId);
  if (status == QTEL_OK)
    QTEL_SockManager_SetState(sockMgr, QTEL_SOCKH_STATE_PDP_ACTIVE);
  else if ( qtelPtr->state < QTEL_STATE_ACTIVE 
            || qtelPtr->net.state != QTEL_NET_STATE_ACTIVE
            || !(QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED) 
                || QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_LTE_REGISTERED)))
  {
    QTEL_SockManager_SetState(sockMgr, QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING);
    return QTEL_ERROR_PENDING;
  }

  return status;
}

QTEL_Status_t QTEL_SockManager_PDP_Deactivate(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;

  if (qtelPtr->state < QTEL_STATE_ACTIVE) {
    return QTEL_ERROR;
  }

  return QTEL_NET_DeactivatePDP(&qtelPtr->net, sockMgr->contextId);
}

// this function will run every tick
void QTEL_SockManager_Loop(QTEL_Socket_HandlerTypeDef *sockMgr)
{
  QTEL_HandlerTypeDef *qtelPtr = sockMgr->qtel;
  uint8_t i;

  if (qtelPtr->state < QTEL_STATE_CHECK_SIMCARD) {
    goto socketClientLoop;
  }

  switch (sockMgr->state) {
  case QTEL_SOCKH_STATE_PDP_ACTIVATING:
    if (QTEL_IsTimeout(qtelPtr, sockMgr->activatingTick, 10000)) {
      QTEL_SockManager_PDP_Activate(sockMgr);
    }
    break; 

  case QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING:
    if (QTEL_IsTimeout(qtelPtr, sockMgr->activatingPendingTick, 3000)) {
      sockMgr->activatingPendingTick = qtelPtr->getTick();
      if (qtelPtr->state >= QTEL_STATE_ACTIVE
          && qtelPtr->net.state == QTEL_NET_STATE_ACTIVE
          && (QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_GPRS_REGISTERED)
              || QTEL_IS_STATUS(qtelPtr, QTEL_STATUS_LTE_REGISTERED)))
      {
        QTEL_SockManager_SetState(&qtelPtr->socketManager, QTEL_SOCKH_STATE_PDP_ACTIVATING);
      }
    }
    break;

  default: break;
  }

socketClientLoop:
  for (i = 0; i < QTEL_NUM_OF_SOCKET; i++) {
    if (sockMgr->sockets[i] != 0)
      QTEL_SockClient_Loop(sockMgr->sockets[i]);
  }
}

int8_t QTEL_SockManager_GetAvailableLinknum(QTEL_Socket_HandlerTypeDef *sockMgr, void *socket)
{
  int16_t i;

  for (i = 0; i < QTEL_NUM_OF_SOCKET; i++)
  {
    if (socket == sockMgr->sockets[i])
      return i;
  }

  // search null
  for (i = 0; i < QTEL_NUM_OF_SOCKET; i++)
  {
    if (sockMgr->sockets[i] == NULL)
      return i;
  }

  return -1;
}

static void onSocketOpened(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;
  uint8_t linkNum = resp->value.number;

  resp++;
  uint16_t err = resp->value.number;

  QTEL_SocketClient_t *sock = qtelPtr->socketManager.sockets[linkNum];
  if (sock == NULL) return;

  if (err == 0) {
    QTEL_SockClient_SetEvents(sock, QTEL_SOCK_EVENT_ON_OPENED);
  }
  else {
    QTEL_SockClient_SetEvents(sock, QTEL_SOCK_EVENT_ON_OPENING_ERROR);

    switch (err) {
    case 550: case 561: case 568: case 569: case 570: case 572: case 573:
      qtelPtr->socketManager.state = QTEL_SOCKH_STATE_NON_ACTIVE;
      break;

    default: break;
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

  if (sock == NULL) return;

  if (strncmp(evt, "recv", 4) == 0) {
    QTEL_SockClient_SetEvents(sock, QTEL_SOCK_EVENT_ON_RECV_DATA_AVAILABLE);
  }
  else if (strncmp(evt, "closed", 6) == 0) {
    QTEL_SockClient_SetEvents(sock, QTEL_SOCK_EVENT_ON_CLOSING);
#if QTEL_DEBUG
    qtelPtr->debug.tcpClosedByServerCounter += 1;
#endif
  }
}


#endif /* QTEL_EN_FEATURE_SOCKET */
