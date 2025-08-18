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
static uint8_t getSockState(QTEL_HandlerTypeDef*, int8_t linkNum, uint8_t *isSSL);
static QTEL_Status_t sockClose(QTEL_SocketClient_t *sock);
static void sockDisconnectWithLinkNum(QTEL_SocketClient_t *sock);
// SSL function
static QTEL_Status_t configSSL(QTEL_SocketClient_t *sock);

QTEL_Status_t QTEL_SockClient_Init(QTEL_SocketClient_t *sock, const char *host, uint16_t port, void *buffer)
{
  uint8_t i = 0;
  char *sockIP = sock->host;
  while (*host != '\0' && i < 63) {
    *sockIP = *host;
    host++;
    sockIP++;
    i++;
  }
  *sockIP = 0;

  sock->port = port;

  if (sock->config.timeout == 0)
    sock->config.timeout = QTEL_SOCK_DEFAULT_TO;

  sock->events = 0;
  sock->linkNum = -1;
  sock->buffer = buffer;
  if (sock->buffer == NULL)
    return QTEL_ERROR;

  sock->state = QTEL_SOCK_STATE_CLOSE;
  return QTEL_OK;
}


QTEL_Status_t QTEL_SockClient_OnNetOpened(QTEL_SocketClient_t *sock)
{
  if (sock == NULL) return QTEL_ERROR;
  if (sock->state == QTEL_SOCK_STATE_WAIT_PDP_ACTIVE) {
    return sockOpen(sock);
  }
  return QTEL_OK;
}


QTEL_Status_t QTEL_SockClient_OnPoweredDown(QTEL_SocketClient_t *sock)
{
  if (sock == NULL) return QTEL_ERROR;
  if (sock->state == QTEL_SOCK_STATE_OPENING
      || QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_OPENING_ERROR))
  {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_OPENING_ERROR);
    sock->state = QTEL_SOCK_STATE_CLOSE;
    if (sock->listeners.onConnectingError) sock->listeners.onConnectingError();
    sockDisconnectWithLinkNum(sock);
  }

  else if (sock->state == QTEL_SOCK_STATE_OPEN || QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_CLOSED)) 
  {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_CLOSED);
    sock->state = QTEL_SOCK_STATE_CLOSE;
    if (sock->listeners.onClosed) sock->listeners.onClosed();
    sockDisconnectWithLinkNum(sock);
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_SockClient_SetEvents(QTEL_SocketClient_t *sock, uint8_t events)
{
  if (sock == NULL) return QTEL_ERROR;
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;
  if (qtelPtr == 0 || qtelPtr->rtos.eventSet == 0) return QTEL_ERROR;

  QTEL_BITS_SET(sock->events, events);
  qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_SOCKCLIENT_NEW_EVT);
  return QTEL_OK;
}

QTEL_Status_t QTEL_SockClient_CheckEvents(QTEL_SocketClient_t *sock)
{
  if (sock == NULL) return QTEL_ERROR;
  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_OPENING)) {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_OPENING);
    sockOpen(sock);
  }
  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_OPENING_ERROR)) {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_OPENING_ERROR);
    sock->tick.opening = 0;
    sock->state = QTEL_SOCK_STATE_CLOSE;
    if (sock->listeners.onConnectingError) sock->listeners.onConnectingError();
    sockDisconnectWithLinkNum(sock);
  }
  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_OPENED)) {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_OPENED);
    sock->tick.opening = 0;
    sock->state = QTEL_SOCK_STATE_OPEN;
    if (sock->listeners.onConnected) sock->listeners.onConnected();
  }
  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_CLOSING)) {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_CLOSING);
    sockClose(sock);
    sockDisconnectWithLinkNum(sock);
  }
  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_CLOSED)) {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_CLOSED);
    sock->tick.closing = 0;
    sock->state = QTEL_SOCK_STATE_CLOSE;
    if (sock->listeners.onClosed) sock->listeners.onClosed();
    sockDisconnectWithLinkNum(sock);
  }
  if (QTEL_BITS_IS(sock->events, QTEL_SOCK_EVENT_ON_RECV_DATA_AVAILABLE)) {
    QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_RECV_DATA_AVAILABLE);
    sockReadRecvData(sock);
  }
  return QTEL_OK;
}


QTEL_Status_t QTEL_SockClient_Loop(QTEL_SocketClient_t *sock)
{
  if (sock == NULL) return QTEL_ERROR;

  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  switch (sock->state) {
  case QTEL_SOCK_STATE_OPENING:
  case QTEL_SOCK_STATE_WAIT_PDP_ACTIVE:
    if (sock->state == QTEL_SOCK_STATE_OPENING && qtelPtr->state >= QTEL_STATE_ACTIVE) {
      if (sock->tick.connecting && QTEL_IsTimeout(qtelPtr, sock->tick.connecting, 150000)) {
        sock->tick.connecting = 0;
        sock->state = QTEL_SOCK_STATE_WAIT_PDP_ACTIVE;
        QTEL_Reboot(qtelPtr);
      }
      break;
    }

    if (sock->tick.opening && QTEL_IsTimeout(qtelPtr, sock->tick.opening, sock->config.openingTimeout)) {
      sock->tick.opening = 0;
      QTEL_SockClient_SetEvents(sock, QTEL_SOCK_EVENT_ON_OPENING_ERROR);
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
QTEL_Status_t QTEL_SockClient_Open(QTEL_SocketClient_t *sock, QTEL_HandlerTypeDef *qtelPtr, uint32_t timeout)
{
  if (qtelPtr->key != QTEL_KEY)
    return QTEL_ERROR;

  sock->socketManager = &qtelPtr->socketManager;

  sock->linkNum = QTEL_SockManager_GetAvailableLinknum(&qtelPtr->socketManager, sock);
  if (sock->linkNum < 0) return QTEL_ERROR;
  sock->socketManager->sockets[sock->linkNum] = sock;
  sock->tick.opening = qtelPtr->getTick();
  sock->config.openingTimeout = timeout;

  QTEL_SockClient_SetEvents(sock, QTEL_SOCK_EVENT_ON_OPENING);

  return QTEL_OK;
}


QTEL_Status_t QTEL_SockClient_Close(QTEL_SocketClient_t *sock, uint32_t timeout)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  sock->state = QTEL_SOCK_STATE_CLOSING;
  sock->tick.closing = qtelPtr->getTick();
  sock->config.closingTimeout = timeout;

  if (sock->linkNum < 0) {
    sock->state = QTEL_SOCK_STATE_CLOSE;
    return QTEL_OK;
  }

  QTEL_SockClient_SetEvents(sock, QTEL_SOCK_EVENT_ON_CLOSING);
  while (sock->state != QTEL_SOCK_STATE_CLOSE) {
    if (sock->tick.closing && QTEL_IsTimeout(qtelPtr, sock->tick.closing, timeout)) {
      sock->state = QTEL_SOCK_STATE_CLOSE;
      sockDisconnectWithLinkNum(sock);
      return QTEL_TIMEOUT;
    }
    qtelPtr->delay(1);
  }
  return QTEL_OK;
}


int QTEL_SockClient_SendData(QTEL_SocketClient_t *sock, uint8_t *data, uint16_t length)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  if (qtelPtr->state != QTEL_STATE_ACTIVE) return -1;
  if (sock->state != QTEL_SOCK_STATE_OPEN) return -1;
  if (sock->linkNum < 0) return -1;
  if (length > 1024) length = 1024;
  else if (length == 0) return 0;

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
      return -1;
    }
  }
  else
  {
    // TODO response SEND FAIL is not handled yet
    if (AT_CommandWrite(&qtelPtr->atCmd, "+QISEND", "> ", "SEND ",
                        data, length,
                        2, paramData, 0, 0) != AT_OK)
    {
      return -1;
    }
  }
  return (int) length;
}


static QTEL_Status_t sockOpen(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;
  QTEL_Status_t status;
  uint8_t isPDPActive = 0;

  if (sock->linkNum < 0) {
    sock->linkNum = QTEL_SockManager_GetAvailableLinknum(sock->socketManager, sock);
    if (sock->linkNum < 0) goto connectingError;
    sock->socketManager->sockets[sock->linkNum] = sock;
  }

  status = QTEL_SockManager_PDP_IsActivate(sock->socketManager, &isPDPActive);
  if (status != QTEL_OK || !isPDPActive) {
    status = QTEL_SockManager_PDP_Activate(&qtelPtr->socketManager);
    if (status != QTEL_OK) {
      if (qtelPtr->socketManager.state == QTEL_SOCKH_STATE_PDP_ACTIVATING_PENDING ||
          qtelPtr->socketManager.state == QTEL_SOCKH_STATE_PDP_ACTIVATING)
      {
        sock->state = QTEL_SOCK_STATE_WAIT_PDP_ACTIVE;
        return QTEL_OK;
      }

      goto connectingError;
    }
  }

  sock->tick.connecting = 0;
  sock->state = QTEL_SOCK_STATE_OPENING;

  if (sockClose(sock) != QTEL_OK) {
    goto connectingError;
  }

  // clear event opening error before open connection
  QTEL_BITS_UNSET(sock->events, QTEL_SOCK_EVENT_ON_OPENING_ERROR);

  if (sock->isSSL == 1) // USE SSL
  {
    if (configSSL(sock) != QTEL_OK) {
      goto connectingError;
    }

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
  if (sock->listeners.onConnecting)
    sock->listeners.onConnecting();

  return QTEL_OK;

connectingError:
  QTEL_SockClient_SetEvents(sock, QTEL_SOCK_EVENT_ON_OPENING_ERROR);
  return QTEL_ERROR;
}

static QTEL_Status_t sockReadRecvData(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;
  if (sock->linkNum < 0) return QTEL_ERROR;

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


static uint8_t getSockState(QTEL_HandlerTypeDef *qtelPtr, int8_t linkNum, uint8_t *isSSL)
{
  AT_Status_t atStatus;

  if (linkNum < 0 || linkNum > QTEL_NUM_OF_SOCKET) return 0;
  if (qtelPtr->state <= QTEL_STATE_STARTING) return 0;

  AT_Data_t paramData[2] = {
      AT_Number(1),             // query type: specific id
      AT_Number(linkNum),
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

  atStatus = AT_Command(&qtelPtr->atCmd, "+QISTATE", 2, paramData, 7, respData);

  if (atStatus == AT_OK) {
    if (respData[0].value.number == linkNum
        && respData[6].value.number == qtelPtr->socketManager.contextId)
    {
      /**
       * 0  "Initial": connection has not been established
       * 1  "Opening": client is connecting or server is trying to listen
       * 2  "Connected": client/incoming connection has been established
       * 3  "Listening": server is listening
       * 4  "Closing": connection is closing
       */
      if (strncmp((const char*)serviceTypestr, "SSLClient", 9) == 0) {
        *isSSL = 1;
      } else {
        *isSSL = 0;
      }
      return (uint8_t) respData[5].value.number;
    }
  }

  return 0;
}


static QTEL_Status_t sockClose(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;
  AT_Data_t paramData[2] = {
      AT_Number(sock->linkNum),
      AT_Number(16), // timeout s
  };

  uint8_t sockState = 0;
  uint8_t isSSL = 0;

  if (sock->linkNum < 0) {
    QTEL_SockClient_SetEvents(sock, QTEL_SOCK_EVENT_ON_CLOSED);
    return QTEL_OK;
  }

  if (qtelPtr->state > QTEL_STATE_STARTING) {
    sockState = getSockState(qtelPtr, sock->linkNum, &isSSL);

    if (sockState != 0) {
      if (isSSL) {
        if (AT_CommandWithTimeout(&qtelPtr->atCmd, "+QSSLCLOSE", 2, paramData, 0, 0, 30000) != AT_OK) {
          return QTEL_ERROR;
        }
      } else {
        if (AT_CommandWithTimeout(&qtelPtr->atCmd, "+QICLOSE", 2, paramData, 0, 0, 30000) != AT_OK) {
          return QTEL_ERROR;
        }
      }
    }
  }

  if (sock->state != QTEL_SOCK_STATE_OPENING) {
    QTEL_SockClient_SetEvents(sock, QTEL_SOCK_EVENT_ON_CLOSED);
  }

  return QTEL_OK;
}

static void sockDisconnectWithLinkNum(QTEL_SocketClient_t *sock)
{
  if (sock->linkNum < 0) return;
  sock->socketManager->sockets[sock->linkNum] = 0;
  sock->linkNum = -1;
  sock->events = 0;
  sock->state = QTEL_SOCK_STATE_CLOSE;
}

static QTEL_Status_t configSSL(QTEL_SocketClient_t *sock)
{
  QTEL_HandlerTypeDef *qtelPtr = sock->socketManager->qtel;

  AT_Data_t paramData[3] = {
      AT_String("sslversion"),
      AT_Number(sock->socketManager->sslcontextId),
      AT_Number(3),
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
