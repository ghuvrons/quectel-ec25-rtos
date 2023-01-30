/*
 * http.c
 *
 *  Created on: Nov 16, 2022
 *      Author: janoko
 */

#include <quectel-ec25/http.h>
#if QTEL_EN_FEATURE_HTTP

#include "../include/quectel-ec25.h"
#include <quectel-ec25/file.h>
#include <quectel-ec25/utils.h>
#include "../events.h"
#include <string.h>
#include <stdlib.h>


static QTEL_Status_t request(QTEL_HTTP_HandlerTypeDef*,
                             QTEL_HTTP_Request_t*,
                             QTEL_HTTP_Response_t*,
                             uint32_t timeout);


static QTEL_Status_t httpConfigure(QTEL_HTTP_HandlerTypeDef*);

static void onGetResponse(void *app, AT_Data_t *resp);
static struct AT_BufferReadTo onReadHead(void *app, AT_Data_t *resp);
static struct AT_BufferReadTo onReadData(void *app, AT_Data_t *resp);


QTEL_Status_t QTEL_HTTP_Init(QTEL_HTTP_HandlerTypeDef *qtelhttp, void *qtelPtr)
{
  if (((QTEL_HandlerTypeDef*)qtelPtr)->key != QTEL_KEY)
    return QTEL_ERROR;

  qtelhttp->qtel = qtelPtr;
  qtelhttp->state = QTEL_HTTP_STATE_AVAILABLE;
  qtelhttp->stateTick = 0;

  AT_Data_t *httpActionResp = malloc(sizeof(AT_Data_t)*3);
  AT_DataSetNumber(httpActionResp, 0);
  AT_DataSetNumber(httpActionResp+1, 0);
  AT_DataSetNumber(httpActionResp+2, 0);
  AT_On(&((QTEL_HandlerTypeDef*)qtelhttp->qtel)->atCmd, "+QHTTPGET",
        qtelhttp->qtel, 3, httpActionResp, onGetResponse);
  AT_On(&((QTEL_HandlerTypeDef*)qtelhttp->qtel)->atCmd, "+HTTPPOSTFILE",
        qtelhttp->qtel, 3, httpActionResp, onGetResponse);

  AT_Data_t *readHeadResp = malloc(sizeof(AT_Data_t)*2);
  uint8_t *readHeadRespStr = malloc(8);
  AT_DataSetBuffer(readHeadResp, readHeadRespStr, 8);
  AT_DataSetNumber(readHeadResp+1, 0);
  AT_ReadIntoBufferOn(&((QTEL_HandlerTypeDef*)qtelhttp->qtel)->atCmd, "+HTTPHEAD",
                      qtelhttp->qtel, 2, readHeadResp, onReadHead);

  AT_Data_t *readDataResp = malloc(sizeof(AT_Data_t)*2);
  uint8_t *readDataRespStr = malloc(8);
  AT_DataSetBuffer(readDataResp, readDataRespStr, 8);
  AT_DataSetNumber(readDataResp+1, 0);
  AT_ReadIntoBufferOn(&((QTEL_HandlerTypeDef*)qtelhttp->qtel)->atCmd, "+HTTPREAD",
                      qtelhttp->qtel, 2, readDataResp, onReadData);

//  AT_On(&qtelhttp->qtel->atCmd, "+HTTP_PEER_CLOSED",
//        qtelhttp->qtel, 2, socketCloseResp, onSocketClosed);
//
//  AT_On(&qtelhttp->qtel->atCmd, "+HTTP_NONET_EVENT",
//        qtelhttp->qtel, 2, socketCloseResp, onSocketClosed);

  return QTEL_OK;
}


QTEL_Status_t QTEL_HTTP_Get(QTEL_HTTP_HandlerTypeDef *qtelhttp,
                            char *url,
                            QTEL_HTTP_Response_t *resp,
                            uint32_t timeout)
{
  QTEL_HandlerTypeDef  *qtelPtr = qtelhttp->qtel;
  QTEL_HTTP_Request_t  req;

  req.url     = url;
  req.method  = 0;
  req.httpData = (const uint8_t*) "Test";
  req.httpDataLength = (uint16_t) strlen((const char*) req.httpData);
  QTEL_FILE_MemoryInfo(&qtelPtr->file);

  return request(qtelhttp, &req, resp, timeout);
}


QTEL_Status_t QTEL_HTTP_SendRequest(QTEL_HTTP_HandlerTypeDef *qtelhttp,
                                    char *url,
                                    uint8_t method,
                                    const uint8_t *httpRequest,
                                    uint16_t httpRequestLength,
                                    QTEL_HTTP_Response_t *resp,
                                    uint32_t timeout)
{
  QTEL_HandlerTypeDef  *qtelPtr = qtelhttp->qtel;
  QTEL_HTTP_Request_t  req;

  req.url             = url;
  req.method          = method;
  req.httpData        = httpRequest;
  req.httpDataLength  = httpRequestLength;
  QTEL_FILE_MemoryInfo(&qtelPtr->file);

  return request(qtelhttp, &req, resp, timeout);
}


static QTEL_Status_t request(QTEL_HTTP_HandlerTypeDef *qtelhttp,
                            QTEL_HTTP_Request_t *req, QTEL_HTTP_Response_t *resp,
                            uint32_t timeout)
{
  QTEL_HandlerTypeDef *qtelPtr    = qtelhttp->qtel;
  QTEL_Status_t       status      = QTEL_TIMEOUT;
  uint32_t            notifEvent;
  AT_Data_t           paramData[3];


  while (qtelhttp->state != QTEL_HTTP_STATE_AVAILABLE) {
    qtelPtr->delay(10);
  }
  qtelhttp->state = QTEL_HTTP_STATE_STARTING;

  if (qtelPtr->net.state < QTEL_NET_STATE_ONLINE) {
    return QTEL_ERROR;
  }

  resp->status            = 0;
  resp->err               = 0;
  resp->code              = 0;
  resp->contentLen        = 0;

  qtelhttp->request  = req;
  qtelhttp->response = resp;

  qtelhttp->contentBufLen = 0;
  qtelhttp->contentReadLen = 0;

  qtelPtr->rtos.eventClear(QTEL_RTOS_EVT_HTTP_NEW_STATE);

  status = httpConfigure(qtelhttp);
  if (status != QTEL_OK) goto endCmd;

  AT_DataSetNumber(&paramData[0], strlen(req->url));
  if (AT_CommandWrite(&qtelPtr->atCmd, "+QHTTPURL", "CONNECT", 0,
                      req->url, strlen(req->url),
                      1, paramData, 0, 0) != AT_OK)
  {
    return QTEL_ERROR;
  }

  qtelPtr->rtos.eventClear(QTEL_RTOS_EVT_HTTP_NEW_STATE);
  AT_DataSetNumber(&paramData[0], 60);
  AT_DataSetNumber(&paramData[1], req->httpDataLength);
  if (AT_CommandWrite(&qtelPtr->atCmd, "+QHTTPGET", "CONNECT", 0,
                      req->httpData, req->httpDataLength,
                      2, paramData, 0, 0) != AT_OK)
  {
    return QTEL_ERROR;
  }

  qtelhttp->state = QTEL_HTTP_STATE_REQUESTING;

  while (qtelPtr->rtos.eventWait(QTEL_RTOS_EVT_HTTP_NEW_STATE,
                                 &notifEvent,
                                 timeout) == AT_OK)
  {
    if (!QTEL_BITS_IS(notifEvent, QTEL_RTOS_EVT_HTTP_NEW_STATE)) goto endCmd;

    switch (qtelhttp->state) {
    case QTEL_HTTP_STATE_GET_RESP:
      AT_DataSetString(&paramData[0], "UFS:/tmp.http");
      AT_DataSetNumber(&paramData[1], 100); // timeout
      if (AT_Command(&qtelPtr->atCmd, "+QHTTPREADFILE", 2, paramData, 0, 0) != AT_OK)
        goto endCmd;
      break;

    case QTEL_HTTP_STATE_GET_BUF_CONTENT:
      if (resp->onGetData) {
        resp->onGetData(resp->contentBuffer, qtelhttp->contentBufLen);
      }
      if (resp->contentLen - qtelhttp->contentReadLen > 0) {
        goto readContent;
      }

    case QTEL_HTTP_STATE_DONE:
      status = QTEL_OK;
      goto endCmd;
      break;
    }

    continue;

  readContent:
    qtelhttp->state = QTEL_HTTP_STATE_READING_CONTENT;

    uint16_t remainingLen = resp->contentLen - qtelhttp->contentReadLen;
    AT_DataSetNumber(&paramData[0], 0);
    AT_DataSetNumber(&paramData[1],
                     (remainingLen > resp->contentBufferSize)?
                         resp->contentBufferSize: remainingLen);
    if (AT_Command(&qtelPtr->atCmd, "+HTTPREAD", 2, paramData, 0, 0) != AT_OK)
      goto endCmd;
  }


endCmd:
  qtelhttp->state = QTEL_HTTP_STATE_AVAILABLE;
  return status;
}


static QTEL_Status_t httpConfigure(QTEL_HTTP_HandlerTypeDef *qtelhttp)
{
  QTEL_HandlerTypeDef *qtelPtr    = qtelhttp->qtel;
  AT_Status_t         status      = QTEL_ERROR;
  AT_Data_t           paramData[3];

  AT_DataSetString(&paramData[0], "contextid");
  AT_DataSetNumber(&paramData[1], qtelPtr->net.contextId);
  status = AT_Command(&qtelPtr->atCmd, "+QHTTPCFG", 2, paramData, 0, 0);
  if (status != AT_OK) return (QTEL_Status_t) status;

  AT_DataSetString(&paramData[0], "requestheader");
  AT_DataSetNumber(&paramData[1], 1);
  status = AT_Command(&qtelPtr->atCmd, "+QHTTPCFG", 2, paramData, 0, 0);
  if (status != AT_OK) return (QTEL_Status_t) status;

  AT_DataSetString(&paramData[0], "responseheader");
  AT_DataSetNumber(&paramData[1], 0);
  status = AT_Command(&qtelPtr->atCmd, "+QHTTPCFG", 2, paramData, 0, 0);
  if (status != AT_OK) return (QTEL_Status_t) status;

  return QTEL_OK;
}


static void onGetResponse(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;
  uint16_t err = 0;

  if (hsim->http.request == 0 || hsim->http.response == 0) return;

  // status error
  hsim->http.response->err = resp->value.number;

  // response code
  resp++;
  hsim->http.response->code = resp->value.number;

  // content length
  resp++;
  hsim->http.response->contentLen = resp->value.number;
  hsim->http.state = QTEL_HTTP_STATE_GET_RESP;
  hsim->rtos.eventSet(QTEL_RTOS_EVT_HTTP_NEW_STATE);
}


static struct AT_BufferReadTo onReadHead(void *app, AT_Data_t *data)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;

  struct AT_BufferReadTo returnBuf = {
      .buffer = 0,
      .readLen = 0,
  };

  data++;
  returnBuf.readLen = data->value.number;

  if (qtelPtr->http.response != 0) {
    returnBuf.buffer = qtelPtr->http.response->headBuffer;
  }

  return returnBuf;
}

static struct AT_BufferReadTo onReadData(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *qtelPtr = (QTEL_HandlerTypeDef*)app;
  struct AT_BufferReadTo returnBuf = {
      .buffer = 0,
      .readLen = 0,
  };

  const char *flag = resp->value.string;

  if (resp->type == AT_NUMBER && resp->value.number == 0)
  {
    qtelPtr->http.state = QTEL_HTTP_STATE_GET_BUF_CONTENT;
    qtelPtr->rtos.eventSet(QTEL_RTOS_EVT_HTTP_NEW_STATE);
  }
  if (strncmp(flag, "DATA", 4) == 0) {
    resp++;
    returnBuf.readLen = resp->value.number;
    qtelPtr->http.contentReadLen += resp->value.number;
    qtelPtr->http.contentBufLen = resp->value.number;

    if (qtelPtr->http.response != 0) {
      returnBuf.buffer = qtelPtr->http.response->contentBuffer;
    }
  }

  return returnBuf;
}



#endif /* QTEL_EN_FEATURE_HTTP */
