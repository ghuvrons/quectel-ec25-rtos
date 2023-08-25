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
// SSL fungsi
static QTEL_Status_t configSSL(QTEL_SocketClient_t *sock);
static uint8_t getSockStateSSL(QTEL_SocketClient_t *sock);

QTEL_Status_t QTEL_SockClient_Init(QTEL_SocketClient_t *sock, const char *host, uint16_t port, void *buffer)
{
  char *sockIP = sock->host;
  while (*host != '\0')
  {
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
  if (sock->state == QTEL_SOCK_STATE_WAIT_PDP_ACTIVE)
  {
    return sockOpen(sock);
  }
  return QTEL_OK;
}

QTEL_Status_t QTEL_SockClient_CheckEvents(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_OPENED))
  {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_OPENED);
    if (sock->listeners.onConnected)
      sock->listeners.onConnected();
  }
  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_CLOSED))
  {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_CLOSED);
    if (sock->state == QTEL_SOCK_STATE_OPEN_PENDING)
    {
      sockOpen(sock);
    }
    else
    {
      sock->state = QTEL_SOCK_STATE_CLOSE;
      sock->tick.reconnDelay = qtelPtr->getTick();
      if (sock->listeners.onClosed)
        sock->listeners.onClosed();
    }
  }
  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_RECV_DATA_AVAILABLE))
  {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_RECV_DATA_AVAILABLE);
    sockReadRecvData(sock);
  }
  return QTEL_OK;
}

QTEL_Status_t QTEL_SockClient_Loop(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  switch (sock->state)
  {
  case QTEL_SOCK_STATE_WAIT_PDP_ACTIVE:
    sockOpen(sock);
    break;

  case QTEL_SOCK_STATE_OPENING:
    if (sock->tick.connecting && QTEL_IsTimeout(qtelPtr, sock->tick.connecting, 30000))
    {
      sock->state = QTEL_SOCK_STATE_OPEN_PENDING;
      QTEL_SockClient_Close(sock);
    }
    break;

  case QTEL_SOCK_STATE_CLOSE:
    if (sock->tick.reconnDelay == 0)
    {
    }
    else if (QTEL_IsTimeout(qtelPtr, sock->tick.reconnDelay, 2000))
    {
      if (qtelPtr->socketManager.state != QTEL_SOCKH_STATE_PDP_ACTIVE)
      {
        QTEL_SockManager_PDP_Activate(&qtelPtr->socketManager);
        sock->state = QTEL_SOCK_STATE_WAIT_PDP_ACTIVE;
        break;
      }

      sockOpen(sock);
    }
    break;

  default:
    break;
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

  if (sock->config.autoReconnect)
  {
    Get_Available_LinkNum(sock->socketManager, sock);
    if (sock->linkNum < 0)
      return QTEL_ERROR;
    sock->socketManager->sockets[sock->linkNum] = sock;
  }

  if (qtelPtr->socketManager.state != QTEL_SOCKH_STATE_PDP_ACTIVE)
  {
    QTEL_SockManager_PDP_Activate(&qtelPtr->socketManager);
    sock->state = QTEL_SOCK_STATE_WAIT_PDP_ACTIVE;
    return QTEL_OK;
  }

  sockOpen(sock);

  return QTEL_OK;
}

QTEL_Status_t QTEL_SockClient_Close(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  if (getSockState(sock) != 0)
  {
    return sockClose(sock);
  }
  else
  {
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

  if (sock->state != QTEL_SOCK_STATE_OPEN)
    return 0;

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

  if (sock->isSSL == 1)
    configSSL(sock);

  if (sock->linkNum == -1)
  {
    Get_Available_LinkNum(sock->socketManager, sock);
    if (sock->linkNum < 0)
      return QTEL_ERROR;
    sock->socketManager->sockets[sock->linkNum] = sock;
  }

  if (sock->isSSL == 1)
  {
    sockState = getSockStateSSL(sock);
  }
  else
  {
    sockState = getSockState(sock);
  }

  if (sockState != 0)
  {
    sockClose(sock);
    sock->state = QTEL_SOCK_STATE_OPEN_PENDING;
    return QTEL_ERROR;
  }

  sock->state = QTEL_SOCK_STATE_OPENING;
  sock->tick.connecting = qtelPtr->getTick();

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

    if (AT_Command(&qtelPtr->atCmd, "+QSSLOPEN", 6, paramData, 0, 0) != AT_OK)
    {
      sock->state = QTEL_SOCK_STATE_OPEN_PENDING;
      return QTEL_ERROR;
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

    if (AT_Command(&qtelPtr->atCmd, "+QIOPEN", 5, paramData, 0, 0) != AT_OK)
    {
      sock->state = QTEL_SOCK_STATE_OPEN_PENDING;
      return QTEL_ERROR;
    }
  }
  sock->listeners.onConnecting();

  return QTEL_OK;
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

  if (sock->linkNum < 0)
    return 0;

  AT_Data_t paramData[2] = {
      AT_Number(1), // query type: specific id
      AT_Number(sock->linkNum),
  };

  uint8_t serviceTypestr[14];
  uint8_t ipStr[17];
  AT_Data_t respData[7] = {
      AT_Number(0),                                      // number: connectID
      AT_Buffer(serviceTypestr, sizeof(serviceTypestr)), // string: service type
      AT_Buffer(ipStr, sizeof(ipStr)),                   // string: IP
      AT_Number(0),                                      // number: remote port
      AT_Number(0),                                      // number: local port
      AT_Number(0),                                      // number: socket state
      AT_Number(0),                                      // number: context Id
  };

  if (AT_Command(&qtelPtr->atCmd, "+QISTATE", 2, paramData, 7, respData) == AT_OK)
  {
    if (respData[0].value.number == sock->linkNum && respData[6].value.number == sock->socketManager->contextId)
    {
      return (uint8_t)respData[5].value.number;
    }
  }

  return 0;
}

static QTEL_Status_t sockClose(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  if (sock->linkNum < 0)
    return QTEL_ERROR;

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
    if (AT_Command(&qtelPtr->atCmd, "+QICLOSE", 1, &paramData, 0, 0) != AT_OK)
    {
      return QTEL_ERROR;
    }
  }

  sock->state = QTEL_SOCK_STATE_CLOSE;
  sock->tick.reconnDelay = qtelPtr->getTick();
  QTEL_BITS_SET(sock->events, QTEL_SOCK_EVENT_ON_CLOSED);
  qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
  return QTEL_OK;
}

static QTEL_Status_t configSSL(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  AT_Data_t paramData[4] = {
      AT_String("sslversion"),
      AT_Number(sock->socketManager->contextId),
      AT_Number(1),
  };

  if (AT_Command(&qtelPtr->atCmd, "+QSSLCFG", 3, paramData, 0, 0) != AT_OK)
  {
    return QTEL_ERROR;
  }

  uint8_t ts[2] = {0xff, 0xff};
  AT_Data_t paramData1[4] = {
      AT_String("ciphersuite"),
      AT_Number(sock->socketManager->contextId),
      AT_Hex(ts),
  };

  if (AT_Command(&qtelPtr->atCmd, "+QSSLCFG", 3, paramData1, 0, 0) != AT_OK)
  {
    return QTEL_ERROR;
  }

  AT_Data_t paramData2[4] = {
      AT_String("seclevel"),
      AT_Number(sock->socketManager->contextId),
      AT_Number(1),
  };

  if (AT_Command(&qtelPtr->atCmd, "+QSSLCFG", 3, paramData2, 0, 0) != AT_OK)
  {
    return QTEL_ERROR;
  }

  return QTEL_OK;
}

#endif /* QTEL_EN_FEATURE_SOCKET */
