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
#include <quectel-ec25/socket-client.h>


#define QTEL_SOCK_DEFAULT_TO 2000

enum {
  QTEL_SOCKMGR_STATE_NET_CLOSE,
  QTEL_SOCKMGR_STATE_NET_OPENING,
  QTEL_SOCKMGR_STATE_NET_OPEN_PENDING,   // cause by fail when opening
  QTEL_SOCKMGR_STATE_NET_OPEN,
};

typedef struct QTEL_Socket_HandlerTypeDef {
  void                *qtel;
  uint8_t             contextId;
  uint8_t             state;
  uint32_t            stateTick;
  uint8_t             socketsNb;
  QTEL_SocketClient_t *sockets[QTEL_NUM_OF_SOCKET];
} QTEL_Socket_HandlerTypeDef;

QTEL_Status_t QTEL_SockManager_Init(QTEL_Socket_HandlerTypeDef*, void *qtelPtr);
void          QTEL_SockManager_SetState(QTEL_Socket_HandlerTypeDef*, uint8_t newState);
QTEL_Status_t QTEL_SockManager_OnNewState(QTEL_Socket_HandlerTypeDef*);
void          QTEL_SockManager_CheckSocketsEvents(QTEL_Socket_HandlerTypeDef*);
void          QTEL_SockManager_Loop(QTEL_Socket_HandlerTypeDef*);

QTEL_Status_t QTEL_SockManager_CheckNetOpen(QTEL_Socket_HandlerTypeDef*);
QTEL_Status_t QTEL_SockManager_NetOpen(QTEL_Socket_HandlerTypeDef*);


#endif /* QTEL_EN_FEATURE_SOCKET */
#endif /* QUECTEL_EC25_SOCKET_H_ */
