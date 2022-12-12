/*
 * socket.h
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#ifndef SIMCOM_7600E_SOCKET_H_
#define SIMCOM_7600E_SOCKET_H_

#include "conf.h"
#if SIM_EN_FEATURE_SOCKET

#include "types.h"
#include "socket-client.h"


#define SIM_SOCK_DEFAULT_TO 2000

enum {
  SIM_SOCKMGR_STATE_NET_CLOSE,
  SIM_SOCKMGR_STATE_NET_OPENING,
  SIM_SOCKMGR_STATE_NET_OPEN_PENDING,   // cause by fail when opening
  SIM_SOCKMGR_STATE_NET_OPEN,
};

typedef struct SIM_Socket_HandlerTypeDef {
  void                *hsim;
  uint8_t             state;
  uint32_t            stateTick;
  uint8_t             socketsNb;
  SIM_SocketClient_t  *sockets[SIM_NUM_OF_SOCKET];
} SIM_Socket_HandlerTypeDef;

SIM_Status_t SIM_SockManager_Init(SIM_Socket_HandlerTypeDef*, void *hsim);
void         SIM_SockManager_SetState(SIM_Socket_HandlerTypeDef*, uint8_t newState);
SIM_Status_t SIM_SockManager_OnNewState(SIM_Socket_HandlerTypeDef*);
void         SIM_SockManager_CheckSocketsEvents(SIM_Socket_HandlerTypeDef*);
void         SIM_SockManager_Loop(SIM_Socket_HandlerTypeDef*);

SIM_Status_t SIM_SockManager_CheckNetOpen(SIM_Socket_HandlerTypeDef*);
SIM_Status_t SIM_SockManager_NetOpen(SIM_Socket_HandlerTypeDef*);


#endif /* SIM_EN_FEATURE_SOCKET */
#endif /* SIMCOM_7600E_SOCKET_H_ */
