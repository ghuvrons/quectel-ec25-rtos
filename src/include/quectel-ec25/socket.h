/*

 * socket.h
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#ifndef QUECTEL_EC25_SOCKET_H_
#define QUECTEL_EC25_SOCKET_H_

#include <quectel-ec25/conf.h>
#if QTEL_EN_FEATURE_SOCKET

#include <quectel-ec25/types.h>


#define QTEL_SOCK_DEFAULT_TO 2000

typedef enum {
  QTEL_SOCKH_STATE_NON_ACTIVE,
  QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING,   // waiting Quectel activated
  QTEL_SOCKH_STATE_PDP_ACTIVATING,
  QTEL_SOCKH_STATE_PDP_ACTIVE,
} QTEL_Socket_State_t;

typedef struct QTEL_Socket_HandlerTypeDef {
  void                *qtel;
  QTEL_Socket_State_t state;

  uint8_t contextId;
  uint8_t sslcontextId;

  uint8_t                     socketsNb;
  struct QTEL_SocketClient_t  *sockets[QTEL_NUM_OF_SOCKET];
} QTEL_Socket_HandlerTypeDef;

QTEL_Status_t QTEL_SockManager_Init(QTEL_Socket_HandlerTypeDef*, void *qtelPtr);
void          QTEL_SockManager_SetState(QTEL_Socket_HandlerTypeDef *, uint8_t newState);
void          QTEL_SockManager_OnNewState(QTEL_Socket_HandlerTypeDef*);
void          QTEL_SockManager_OnReboot(QTEL_Socket_HandlerTypeDef*);
void          QTEL_SockManager_CheckSocketsEvents(QTEL_Socket_HandlerTypeDef*);
QTEL_Status_t QTEL_SockManager_PDP_Activate(QTEL_Socket_HandlerTypeDef*);
QTEL_Status_t QTEL_SockManager_PDP_Deactivate(QTEL_Socket_HandlerTypeDef *sockMgr);
void          QTEL_SockManager_Loop(QTEL_Socket_HandlerTypeDef*);


#endif /* QTEL_EN_FEATURE_SOCKET */
#endif /* QUECTEL_EC25_SOCKET_H_ */
