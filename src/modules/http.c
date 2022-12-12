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
static void onGetResponse(void *app, AT_Data_t *resp);
static struct AT_BufferReadTo onReadHead(void *app, AT_Data_t *resp);
static struct AT_BufferReadTo onReadData(void *app, AT_Data_t *resp);


QTEL_Status_t QTEL_HTTP_Init(QTEL_HTTP_HandlerTypeDef *hsimHttp, void *hsim)
{
  if (((QTEL_HandlerTypeDef*)hsim)->key != QTEL_KEY)
    return QTEL_ERROR;

  hsimHttp->hsim = hsim;
  hsimHttp->state = QTEL_HTTP_STATE_AVAILABLE;
  hsimHttp->stateTick = 0;

  AT_Data_t *httpActionResp = malloc(sizeof(AT_Data_t)*3);
  AT_DataSetNumber(httpActionResp, 0);
  AT_DataSetNumber(httpActionResp+1, 0);
  AT_DataSetNumber(httpActionResp+2, 0);
  AT_On(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+HTTPACTION",
        (QTEL_HandlerTypeDef*) hsim, 3, httpActionResp, onGetResponse);
  AT_On(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+HTTPPOSTFILE",
        (QTEL_HandlerTypeDef*) hsim, 3, httpActionResp, onGetResponse);

  AT_Data_t *readHeadResp = malloc(sizeof(AT_Data_t)*2);
  uint8_t *readHeadRespStr = malloc(8);
  AT_DataSetBuffer(readHeadResp, readHeadRespStr, 8);
  AT_DataSetNumber(readHeadResp+1, 0);
  AT_ReadIntoBufferOn(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+HTTPHEAD",
        (QTEL_HandlerTypeDef*) hsim, 2, readHeadResp, onReadHead);

  AT_Data_t *readDataResp = malloc(sizeof(AT_Data_t)*2);
  uint8_t *readDataRespStr = malloc(8);
  AT_DataSetBuffer(readDataResp, readDataRespStr, 8);
  AT_DataSetNumber(readDataResp+1, 0);
  AT_ReadIntoBufferOn(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+HTTPREAD",
        (QTEL_HandlerTypeDef*) hsim, 2, readDataResp, onReadData);

//  AT_On(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+HTTP_PEER_CLOSED",
//        (QTEL_HandlerTypeDef*) hsim, 2, socketCloseResp, onSocketClosed);
//
//  AT_On(&((QTEL_HandlerTypeDef*)hsim)->atCmd, "+HTTP_NONET_EVENT",
//        (QTEL_HandlerTypeDef*) hsim, 2, socketCloseResp, onSocketClosed);

  return QTEL_OK;
}


QTEL_Status_t QTEL_HTTP_Get(QTEL_HTTP_HandlerTypeDef *hsimHttp,
                          char *url,
                          QTEL_HTTP_Response_t *resp,
                          uint32_t timeout)
{
  QTEL_HandlerTypeDef  *hsim       = hsimHttp->hsim;
  QTEL_HTTP_Request_t  req;

  req.url     = url;
  req.method  = 0;
  req.httpData = "Test";
  req.httpDataLength = strlen(req.httpData);
  QTEL_FILE_MemoryInfo(&hsim->file);

  return request(hsimHttp, &req, resp, timeout);
}


QTEL_Status_t QTEL_HTTP_SendRequest(QTEL_HTTP_HandlerTypeDef *hsimHttp, char *url,
                                  uint8_t method,
                                  const uint8_t *httpRequest,
                                  uint16_t httpRequestLength,
                                  QTEL_HTTP_Response_t *resp,
                                  uint32_t timeout)
{
  QTEL_HandlerTypeDef  *hsim       = hsimHttp->hsim;
  QTEL_HTTP_Request_t  req;

  req.url     = url;
  req.method  = method;
  req.httpData = httpRequest;
  req.httpDataLength = httpRequestLength;
  QTEL_FILE_MemoryInfo(&hsim->file);

  return request(hsimHttp, &req, resp, timeout);
}


static QTEL_Status_t request(QTEL_HTTP_HandlerTypeDef *hsimHttp,
                            QTEL_HTTP_Request_t *req, QTEL_HTTP_Response_t *resp,
                            uint32_t timeout)
{
  QTEL_HandlerTypeDef  *hsim       = hsimHttp->hsim;
  QTEL_Status_t        status      = QTEL_TIMEOUT;
  uint32_t            notifEvent;
  AT_Data_t           paramData[3];


  while (hsimHttp->state != QTEL_HTTP_STATE_AVAILABLE) {
    hsim->delay(10);
  }
  hsimHttp->state = QTEL_HTTP_STATE_STARTING;

  if (hsim->net.state < QTEL_NET_STATE_ONLINE) {
    return QTEL_ERROR;
  }

  resp->status            = 0;
  resp->err               = 0;
  resp->code              = 0;
  resp->contentLen        = 0;

  hsim->http.request  = req;
  hsim->http.response = resp;

  hsim->http.contentBufLen = 0;
  hsim->http.contentReadLen = 0;

  if (AT_Command(&hsim->atCmd, "+HTTPINIT", 0, 0, 0, 0) != AT_OK) goto endCmd;

  AT_DataSetString(&paramData[0], "URL");
  AT_DataSetString(&paramData[1], (char*) req->url);
  if (AT_Command(&hsim->atCmd, "+HTTPPARA", 2, paramData, 0, 0) != AT_OK) goto endCmd;


  hsimHttp->state = QTEL_HTTP_STATE_REQUESTING;
  hsim->rtos.eventClear(QTEL_RTOS_EVT_HTTP_NEW_STATE);

  if (req->httpData != 0 && req->httpDataLength != 0) {
    // preparing file storage
    if (QTEL_FILE_ChangeDir(&hsim->file, "E:/modem_http/") != QTEL_OK) {
      if (QTEL_FILE_ChangeDir(&hsim->file, "E:/") != QTEL_OK)
        goto endCmd;

      if (QTEL_FILE_MakeDir(&hsim->file, "modem_http") != QTEL_OK)
        goto endCmd;

      if (QTEL_FILE_ChangeDir(&hsim->file, "E:/modem_http/") != QTEL_OK)
        goto endCmd;
    }

    if (QTEL_FILE_IsFileExist(&hsim->file, "E:/request.http") == QTEL_OK) {
      QTEL_FILE_RemoveFile(&hsim->file, "E:/request.http");
    }

    if (QTEL_FILE_CreateAndWriteFile(&hsim->file, "E:/request.http",
                                    req->httpData, req->httpDataLength) != QTEL_OK) {
      return QTEL_ERROR;
    }

    AT_DataSetString(&paramData[0], "request.http");
    AT_DataSetNumber(&paramData[1], 3);
    AT_DataSetNumber(&paramData[2], req->method);
    if (AT_Command(&hsim->atCmd, "+HTTPPOSTFILE", 3, paramData, 0, 0) != AT_OK) goto endCmd;

  } else {
    AT_DataSetNumber(&paramData[0], req->method);
    if (AT_Command(&hsim->atCmd, "+HTTPACTION", 1, paramData, 0, 0) != AT_OK) goto endCmd;
  }

  while (hsim->rtos.eventWait(QTEL_RTOS_EVT_HTTP_NEW_STATE, &notifEvent, timeout) == AT_OK) {
    if (!QTEL_BITS_IS(notifEvent, QTEL_RTOS_EVT_HTTP_NEW_STATE)) goto endCmd;

    switch (hsimHttp->state) {
    case QTEL_HTTP_STATE_GET_RESP:
      if (AT_Command(&hsim->atCmd, "+HTTPHEAD", 0, 0, 0, 0) != AT_OK) goto endCmd;
      if (resp->contentLen > 0) {
        goto readContent;
      }
      hsimHttp->state = QTEL_HTTP_STATE_GET_BUF_CONTENT;
      hsim->rtos.eventSet(QTEL_RTOS_EVT_HTTP_NEW_STATE);
      break;

    case QTEL_HTTP_STATE_GET_BUF_CONTENT:
      if (resp->onGetData) {
        resp->onGetData(resp->contentBuffer, hsimHttp->contentBufLen);
      }
      if (resp->contentLen - hsimHttp->contentReadLen > 0) {
        goto readContent;
      }

    case QTEL_HTTP_STATE_DONE:
      status = QTEL_OK;
      goto endCmd;
      break;
    }

    continue;

  readContent:
    hsimHttp->state = QTEL_HTTP_STATE_READING_CONTENT;

    uint16_t remainingLen = resp->contentLen - hsimHttp->contentReadLen;
    AT_DataSetNumber(&paramData[0], 0);
    AT_DataSetNumber(&paramData[1],
                     (remainingLen > resp->contentBufferSize)?
                         resp->contentBufferSize: remainingLen);
    if (AT_Command(&hsim->atCmd, "+HTTPREAD", 2, paramData, 0, 0) != AT_OK) goto endCmd;
  }


endCmd:
  if (hsimHttp->state > QTEL_HTTP_STATE_STARTING) {
    AT_Command(&hsim->atCmd, "+HTTPTERM", 0, 0, 0, 0);
  }
  hsimHttp->state = QTEL_HTTP_STATE_AVAILABLE;
  return status;
}

static void onGetResponse(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;

  if (hsim->http.request == 0 || hsim->http.response == 0) return;
  if (resp->value.number != hsim->http.request->method) return;

  resp++;
  hsim->http.response->code = resp->value.number;

  resp++;
  hsim->http.response->contentLen = resp->value.number;
  hsim->http.state = QTEL_HTTP_STATE_GET_RESP;
  hsim->rtos.eventSet(QTEL_RTOS_EVT_HTTP_NEW_STATE);
}


static struct AT_BufferReadTo onReadHead(void *app, AT_Data_t *data)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;
  struct AT_BufferReadTo returnBuf = {
      .buffer = 0,
      .bufferSize = 0,
      .readLen = 0,
  };

  const char *flag = data->value.string;

  data++;
  returnBuf.readLen = data->value.number;

  if (hsim->http.response != 0) {
    returnBuf.buffer = hsim->http.response->headBuffer;
    returnBuf.bufferSize = hsim->http.response->headBufferSize;
  }

  return returnBuf;
}

static struct AT_BufferReadTo onReadData(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;
  struct AT_BufferReadTo returnBuf = {
      .buffer = 0,
      .bufferSize = 0,
      .readLen = 0,
  };

  char *flag = resp->value.string;

  if (resp->type == AT_NUMBER && resp->value.number == 0)
  {
    hsim->http.state = QTEL_HTTP_STATE_GET_BUF_CONTENT;
    hsim->rtos.eventSet(QTEL_RTOS_EVT_HTTP_NEW_STATE);
  }
  if (strncmp(flag, "DATA", 4) == 0) {
    resp++;
    returnBuf.readLen = resp->value.number;
    hsim->http.contentReadLen += resp->value.number;
    hsim->http.contentBufLen = resp->value.number;

    if (hsim->http.response != 0) {
      returnBuf.buffer = hsim->http.response->contentBuffer;
      returnBuf.bufferSize = hsim->http.response->contentBufferSize;
    }
  }

  return returnBuf;
}



#endif /* QTEL_EN_FEATURE_HTTP */
