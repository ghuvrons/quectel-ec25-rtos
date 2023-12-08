/*
 * socket-client.c
 *
 *  Created on: Nov 10, 2022
 *      Author: janoko
 */

#include <quectel-ec25/socket-client.h>

#if QTEL_EN_FEATURE_SOCKET
#include "../events.h"
#include <quectel-ec25.h>
#include <quectel-ec25/socket.h>
#include <quectel-ec25/utils.h>
#include <string.h>

#define Get_Available_LinkNum(hsimsock, sock)                               \
  {                                                                         \
    for (int16_t i = 0; i < QTEL_NUM_OF_SOCKET; i++)                        \
    {                                                                       \
      if ((hsimsock)->sockets[i] == NULL || sock == (hsimsock)->sockets[i]) \
      {                                                                     \
        sock->linkNum = i;                                                  \
        break;                                                              \
      }                                                                     \
    }                                                                       \
  }

static QTEL_Status_t sockOpen(QTEL_SocketClient_t *sock);
static QTEL_Status_t sockReadRecvData(QTEL_SocketClient_t *sock);
static uint8_t getSockState(QTEL_SocketClient_t *sock);
static QTEL_Status_t sockClose(QTEL_SocketClient_t *sock);
// SSL function
static QTEL_Status_t configSSL(QTEL_SocketClient_t *sock);
static uint8_t getSockStateSSL(QTEL_SocketClient_t *sock);

QTEL_Status_t QTEL_SockClient_Init(QTEL_SocketClient_t *sock, const char *host, uint16_t port, void *buffer)
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

  sock->state = QTEL_SOCK_STATE_CLOSE;
  return QTEL_OK;
}


QTEL_Status_t QTEL_SockClient_OnNetOpened(QTEL_SocketClient_t *sock)
{
  if (sock->state == QTEL_SOCK_STATE_WAIT_PDP_ACTIVE) {
    return sockOpen(sock);
  }
  return QTEL_OK;
}


QTEL_Status_t QTEL_SockClient_OnReboot(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  if (sock->state == QTEL_SOCK_STATE_OPENING) {
    sock->state = QTEL_SOCK_STATE_WAIT_PDP_ACTIVE;
  }
  if (sock->state == QTEL_SOCK_STATE_OPEN_ERROR) {
    sock->state = QTEL_SOCK_STATE_WAIT_PDP_ACTIVE;
  }
  if (sock->state == QTEL_SOCK_STATE_OPEN) {
    sock->state = QTEL_SOCK_STATE_CLOSE;
    sock->tick.reconnDelay = qtelPtr->getTick();
    if (sock->listeners.onClosed) sock->listeners.onClosed();
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_SockClient_SetEvents(QTEL_SocketClient_t *sock, uint8_t events)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;
  if (qtelPtr == 0 || qtelPtr->rtos.eventSet == 0) return QTEL_ERROR;

  QTEL_BITS_SET(sock->events, events);
  qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
  return QTEL_OK;
}

QTEL_Status_t QTEL_SockClient_CheckEvents(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_OPENING)) {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_OPENING);
    sockOpen(sock);
  }
  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_OPENED)) {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_OPENED);
    sock->state = QTEL_SOCK_STATE_OPEN;
    if (sock->listeners.onConnected) sock->listeners.onConnected();
  }
  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_CLOSED)) {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_CLOSED);
    sock->state = QTEL_SOCK_STATE_CLOSE;
    sock->tick.reconnDelay = qtelPtr->getTick();
    if (sock->listeners.onClosed) sock->listeners.onClosed();
  }
  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_RECV_DATA_AVAILABLE)) {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_RECV_DATA_AVAILABLE);
    sockReadRecvData(sock);
  }
  return QTEL_OK;
}


QTEL_Status_t QTEL_SockClient_Loop(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  switch (sock->state) {
  case QTEL_SOCK_STATE_OPENING:
    if (sock->tick.connecting && QTEL_IsTimeout(qtelPtr, sock->tick.connecting, 150000)) {
      sock->tick.connecting = 0;
      sock->state = QTEL_SOCK_STATE_WAIT_PDP_ACTIVE;
      QTEL_Reboot(qtelPtr);
    }
    break;

  case QTEL_SOCK_STATE_OPEN_ERROR:
    if (sock->tick.reconnDelay && QTEL_IsTimeout(qtelPtr, sock->tick.reconnDelay, 1000)) {
      sockOpen(sock);
    }
    break;

  case QTEL_SOCK_STATE_CLOSE:
    if (sock->tick.reconnDelay != 0 && QTEL_IsTimeout(qtelPtr, sock->tick.reconnDelay, 2000)) {
      sockOpen(sock);
    }
    break;

  default: break;
  }

  return QTEL_OK;
}


void QTEL_SockClient_SetBuffer(QTEL_SocketClient_t *sock, void *buffer)
{
  sock->buffer = buffer;
}

/**
 * @brief Open the socket
 * @param sock
 * @param qtelPtr
 * @return
 */
QTEL_Status_t QTEL_SockClient_Open(QTEL_SocketClient_t *sock,
                                   QTEL_HandlerTypeDef *qtelPtr)
{
  if (qtelPtr->key != QTEL_KEY)
    return QTEL_ERROR;

  sock->linkNum = -1;
  sock->socketManager = &qtelPtr->socketManager;

  if (sock->config.autoReconnect) {
    Get_Available_LinkNum(sock->socketManager, sock);
    if (sock->linkNum < 0) return QTEL_ERROR;
    sock->socketManager->sockets[sock->linkNum] = sock;
  }

  QTEL_SockClient_SetEvents(sock, QTEL_SOCK_EVENT_ON_OPENING);

  return QTEL_OK;
}


QTEL_Status_t QTEL_SockClient_Close(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  if (getSockState(sock) != 0) {
    return sockClose(sock);
  }
  else {
    sock->state = QTEL_SOCK_STATE_CLOSE;
    sock->tick.reconnDelay = qtelPtr->getTick();
    QTEL_BITS_SET(sock->events, QTEL_SOCK_EVENT_ON_CLOSED);
    qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
    return QTEL_OK;
  }
}


uint16_t QTEL_SockClient_SendData(QTEL_SocketClient_t *sock, uint8_t *data, uint16_t length)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  if (sock->state != QTEL_SOCK_STATE_OPEN) return 0;

  AT_Data_t paramData[2] = {
      AT_Number(sock->linkNum),
      AT_Number(length),
  };

  if (sock->isSSL == 1) // USE SSL
  {
    if (AT_CommandWrite(&qtelPtr->atCmd, "+QSSLSEND", "> ", "SEND ",
                        data, length,
                        2, paramData, 0, 0) != AT_OK)
    {
      return 0;
    }
  }
  else
  {
    // [TODO]: response SEND FAIL is not handled yet
    if (AT_CommandWrite(&qtelPtr->atCmd, "+QISEND", "> ", "SEND ",
                        data, length,
                        2, paramData, 0, 0) != AT_OK)
    {
      return 0;
    }
  }
  return length;
}


static QTEL_Status_t sockOpen(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;
  uint8_t sockState = 0;
  QTEL_Status_t status;

  if (sock->linkNum == -1) {
    Get_Available_LinkNum(sock->socketManager, sock);
    if (sock->linkNum < 0) return QTEL_ERROR;
    sock->socketManager->sockets[sock->linkNum] = sock;
  }

  status = QTEL_SockManager_PDP_Activate(&qtelPtr->socketManager);
  if (status != QTEL_OK) {
    if (qtelPtr->socketManager.state == QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING ||
        qtelPtr->socketManager.state == QTEL_SOCKH_STATE_PDP_ACTIVATING)
    {
      sock->state = QTEL_SOCK_STATE_WAIT_PDP_ACTIVE;
      return QTEL_OK;
    }

    sock->state = QTEL_SOCK_STATE_CLOSE;
    return QTEL_ERROR;
  }

  sock->state = QTEL_SOCK_STATE_OPENING;

  if (sock->isSSL == 1)
  {
    configSSL(sock);
    sockState = getSockStateSSL(sock);
  }
  else
  {
    sockState = getSockState(sock);
  }

  if (sockState != 0) {
    if (sockClose(sock) != QTEL_OK) {
      goto connectingError;
    }
  }

  if (sock->isSSL == 1) // USE SSL
  {
    AT_Data_t paramData[6] = {
        AT_Number(sock->socketManager->contextId),
        AT_Number(sock->socketManager->sslcontextId),
        AT_Number(sock->linkNum),
        AT_String(sock->host),
        AT_Number(sock->port),
        AT_Number(0),
    };

    if (AT_Command(&qtelPtr->atCmd, "+QSSLOPEN", 6, paramData, 0, 0) != AT_OK) {
      goto connectingError;
    }
  }
  else // NO USE SSL
  {
    AT_Data_t paramData[6] = {
        AT_Number(sock->socketManager->contextId),
        AT_Number(sock->linkNum),
        AT_String("TCP"),
        AT_String(sock->host),
        AT_Number(sock->port),
    };

    if (AT_Command(&qtelPtr->atCmd, "+QIOPEN", 5, paramData, 0, 0) != AT_OK) {
      goto connectingError;
    }
  }

  sock->tick.connecting = qtelPtr->getTick();
  sock->listeners.onConnecting();

  return QTEL_OK;

connectingError:
  sock->tick.reconnDelay = qtelPtr->getTick();
  sock->state = QTEL_SOCK_STATE_OPEN_ERROR;
  return QTEL_ERROR;
}

static QTEL_Status_t sockReadRecvData(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;
  AT_Data_t paramData[1] = {
      AT_Number(sock->linkNum),
  };
  AT_Data_t respData[1] = {
      AT_Number(0),
  };

  if (sock->isSSL == 1) // USE SSL
  {
    if (AT_CommandReadInto(&qtelPtr->atCmd, "+QSSLRECV", sock->buffer, (uint16_t *)&respData->value.number, 1, paramData, 1, respData) != AT_OK)
    {
      return QTEL_ERROR;
    }
  }
  else // no use SSL
  {
    if (AT_CommandReadInto(&qtelPtr->atCmd, "+QIRD", sock->buffer, (uint16_t *)&respData->value.number, 1, paramData, 1, respData) != AT_OK)
    {
      return QTEL_ERROR;
    }
  }
  return QTEL_OK;
}

static uint8_t getSockStateSSL(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  if (sock->linkNum < 0)
    return 0;

  AT_Data_t paramData[2] = {
      AT_Number(sock->linkNum),
  };

  uint8_t serviceTypestr[14];
  uint8_t ipStr[17];
  AT_Data_t respData[7] = {
      AT_Number(0),                                      // number: clientID
      AT_Buffer(serviceTypestr, sizeof(serviceTypestr)), // string: SSLClient
      AT_Buffer(ipStr, sizeof(ipStr)),                   // string: IP
      AT_Number(0),                                      // number: remote port
      AT_Number(0),                                      // number: local port
      AT_Number(0),                                      // number: socket state
      AT_Number(0),                                      // number: PDP_ctxID/contextId
  };

  if (AT_Command(&qtelPtr->atCmd, "+QSSLSTATE", 1, paramData, 7, respData) == AT_OK)
  {
    if (respData[0].value.number == sock->linkNum && respData[6].value.number == sock->socketManager->contextId)
    {
      return (uint8_t)respData[5].value.number;
    }
  }

  return 0;
}

static uint8_t getSockState(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  if (sock->linkNum < 0) return 0;

  AT_Data_t paramData[2] = {
      AT_Number(1),             // query type: specific id
      AT_Number(sock->linkNum),
  };

  uint8_t serviceTypestr[14];
  uint8_t ipStr[17];
  AT_Data_t respData[7] = {
      AT_Number(0),                                       // number: connectID
      AT_Buffer(serviceTypestr, sizeof(serviceTypestr)),  // string: service type
      AT_Buffer(ipStr, sizeof(ipStr)),                    // string: IP
      AT_Number(0),                                       // number: remote port
      AT_Number(0),                                       // number: local port
      AT_Number(0),                                       // number: socket state
      AT_Number(0),                                       // number: context Id
  };

  if (AT_Command(&qtelPtr->atCmd, "+QISTATE", 2, paramData, 7, respData) == AT_OK) {
    if (respData[0].value.number == sock->linkNum
        && respData[6].value.number == sock->socketManager->contextId)
    {
      return (uint8_t) respData[5].value.number;
    }
  }

  return 0;
}


static QTEL_Status_t sockClose(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  if (sock->linkNum < 0) return QTEL_ERROR;

  if (qtelPtr->state == QTEL_STATE_ACTIVE) {
    AT_Data_t paramData = AT_Number(sock->linkNum);
    if (sock->isSSL == 1) // USE SSL
    {
      if (AT_Command(&qtelPtr->atCmd, "+QSSLCLOSE", 1, &paramData, 0, 0) != AT_OK)
      {
        return QTEL_ERROR;
      }
    }
    else // NO USE SSL
    {
      if (AT_Command(&qtelPtr->atCmd, "+QICLOSE", 1, &paramData, 0, 0) != AT_OK) {
        return QTEL_ERROR;
      }
    }
  }

  if (sock->state != QTEL_SOCK_STATE_OPENING) {
    sock->state = QTEL_SOCK_STATE_CLOSE;
    sock->tick.reconnDelay = qtelPtr->getTick();
    QTEL_BITS_SET(sock->events, QTEL_SOCK_EVENT_ON_CLOSED);
    qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
  }

  return QTEL_OK;
}

static QTEL_Status_t configSSL(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  AT_Data_t paramData[4] = {
      AT_String("sslversion"),
      AT_Number(sock->socketManager->sslcontextId),
      AT_Number(4),
  };

  if (AT_Command(&qtelPtr->atCmd, "+QSSLCFG", 3, paramData, 0, 0) != AT_OK)
  {
    return QTEL_ERROR;
  }

  AT_Data_t paramData1[4] = {
      AT_String("ciphersuite"),
      AT_Number(sock->socketManager->sslcontextId),
      AT_Bytes("0XFFFF", 6),
  };

  if (AT_Command(&qtelPtr->atCmd, "+QSSLCFG", 3, paramData1, 0, 0) != AT_OK)
  {
    return QTEL_ERROR;
  }

  AT_Data_t paramData2[4] = {
      AT_String("seclevel"),
      AT_Number(sock->socketManager->sslcontextId),
      AT_Number(0),
  };

  if (AT_Command(&qtelPtr->atCmd, "+QSSLCFG", 3, paramData2, 0, 0) != AT_OK)
  {
    return QTEL_ERROR;
  }

  return QTEL_OK;
}

#endif /* QTEL_EN_FEATURE_SOCKET */
