/*
 * http.c
 *
 *  Created on: Nov 16, 2022
 *      Author: janoko
 */

#include "../include/simcom/http.h"
#if SIM_EN_FEATURE_HTTP

#include "../include/simcom.h"
#include "../include/simcom/file.h"
#include "../include/simcom/utils.h"
#include "../events.h"
#include <string.h>
#include <stdlib.h>


static SIM_Status_t request(SIM_HTTP_HandlerTypeDef*,
                            SIM_HTTP_Request_t*,
                            SIM_HTTP_Response_t*,
                            uint32_t timeout);
static void onGetResponse(void *app, AT_Data_t *resp);
static struct AT_BufferReadTo onReadHead(void *app, AT_Data_t *resp);
static struct AT_BufferReadTo onReadData(void *app, AT_Data_t *resp);


SIM_Status_t SIM_HTTP_Init(SIM_HTTP_HandlerTypeDef *hsimHttp, void *hsim)
{
  if (((SIM_HandlerTypeDef*)hsim)->key != SIM_KEY)
    return SIM_ERROR;

  hsimHttp->hsim = hsim;
  hsimHttp->state = SIM_HTTP_STATE_AVAILABLE;
  hsimHttp->stateTick = 0;

  AT_Data_t *httpActionResp = malloc(sizeof(AT_Data_t)*3);
  AT_DataSetNumber(httpActionResp, 0);
  AT_DataSetNumber(httpActionResp+1, 0);
  AT_DataSetNumber(httpActionResp+2, 0);
  AT_On(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+HTTPACTION",
        (SIM_HandlerTypeDef*) hsim, 3, httpActionResp, onGetResponse);
  AT_On(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+HTTPPOSTFILE",
        (SIM_HandlerTypeDef*) hsim, 3, httpActionResp, onGetResponse);

  AT_Data_t *readHeadResp = malloc(sizeof(AT_Data_t)*2);
  uint8_t *readHeadRespStr = malloc(8);
  AT_DataSetBuffer(readHeadResp, readHeadRespStr, 8);
  AT_DataSetNumber(readHeadResp+1, 0);
  AT_ReadIntoBufferOn(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+HTTPHEAD",
        (SIM_HandlerTypeDef*) hsim, 2, readHeadResp, onReadHead);

  AT_Data_t *readDataResp = malloc(sizeof(AT_Data_t)*2);
  uint8_t *readDataRespStr = malloc(8);
  AT_DataSetBuffer(readDataResp, readDataRespStr, 8);
  AT_DataSetNumber(readDataResp+1, 0);
  AT_ReadIntoBufferOn(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+HTTPREAD",
        (SIM_HandlerTypeDef*) hsim, 2, readDataResp, onReadData);

//  AT_On(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+HTTP_PEER_CLOSED",
//        (SIM_HandlerTypeDef*) hsim, 2, socketCloseResp, onSocketClosed);
//
//  AT_On(&((SIM_HandlerTypeDef*)hsim)->atCmd, "+HTTP_NONET_EVENT",
//        (SIM_HandlerTypeDef*) hsim, 2, socketCloseResp, onSocketClosed);

  return SIM_OK;
}


SIM_Status_t SIM_HTTP_Get(SIM_HTTP_HandlerTypeDef *hsimHttp,
                          char *url,
                          SIM_HTTP_Response_t *resp,
                          uint32_t timeout)
{
  SIM_HandlerTypeDef  *hsim       = hsimHttp->hsim;
  SIM_HTTP_Request_t  req;

  req.url     = url;
  req.method  = 0;
  req.httpData = "Test";
  req.httpDataLength = strlen(req.httpData);
  SIM_FILE_MemoryInfo(&hsim->file);

  return request(hsimHttp, &req, resp, timeout);
}


SIM_Status_t SIM_HTTP_SendRequest(SIM_HTTP_HandlerTypeDef *hsimHttp, char *url,
                                  uint8_t method,
                                  const uint8_t *httpRequest,
                                  uint16_t httpRequestLength,
                                  SIM_HTTP_Response_t *resp,
                                  uint32_t timeout)
{
  SIM_HandlerTypeDef  *hsim       = hsimHttp->hsim;
  SIM_HTTP_Request_t  req;

  req.url     = url;
  req.method  = method;
  req.httpData = httpRequest;
  req.httpDataLength = httpRequestLength;
  SIM_FILE_MemoryInfo(&hsim->file);

  return request(hsimHttp, &req, resp, timeout);
}


static SIM_Status_t request(SIM_HTTP_HandlerTypeDef *hsimHttp,
                            SIM_HTTP_Request_t *req, SIM_HTTP_Response_t *resp,
                            uint32_t timeout)
{
  SIM_HandlerTypeDef  *hsim       = hsimHttp->hsim;
  SIM_Status_t        status      = SIM_TIMEOUT;
  uint32_t            notifEvent;
  AT_Data_t           paramData[3];


  while (hsimHttp->state != SIM_HTTP_STATE_AVAILABLE) {
    hsim->delay(10);
  }
  hsimHttp->state = SIM_HTTP_STATE_STARTING;

  if (hsim->net.state < SIM_NET_STATE_ONLINE) {
    return SIM_ERROR;
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


  hsimHttp->state = SIM_HTTP_STATE_REQUESTING;
  hsim->rtos.eventClear(SIM_RTOS_EVT_HTTP_NEW_STATE);

  if (req->httpData != 0 && req->httpDataLength != 0) {
    // preparing file storage
    if (SIM_FILE_ChangeDir(&hsim->file, "E:/modem_http/") != SIM_OK) {
      if (SIM_FILE_ChangeDir(&hsim->file, "E:/") != SIM_OK)
        goto endCmd;

      if (SIM_FILE_MakeDir(&hsim->file, "modem_http") != SIM_OK)
        goto endCmd;

      if (SIM_FILE_ChangeDir(&hsim->file, "E:/modem_http/") != SIM_OK)
        goto endCmd;
    }

    if (SIM_FILE_IsFileExist(&hsim->file, "E:/request.http") == SIM_OK) {
      SIM_FILE_RemoveFile(&hsim->file, "E:/request.http");
    }

    if (SIM_FILE_CreateAndWriteFile(&hsim->file, "E:/request.http",
                                    req->httpData, req->httpDataLength) != SIM_OK) {
      return SIM_ERROR;
    }

    AT_DataSetString(&paramData[0], "request.http");
    AT_DataSetNumber(&paramData[1], 3);
    AT_DataSetNumber(&paramData[2], req->method);
    if (AT_Command(&hsim->atCmd, "+HTTPPOSTFILE", 3, paramData, 0, 0) != AT_OK) goto endCmd;

  } else {
    AT_DataSetNumber(&paramData[0], req->method);
    if (AT_Command(&hsim->atCmd, "+HTTPACTION", 1, paramData, 0, 0) != AT_OK) goto endCmd;
  }

  while (hsim->rtos.eventWait(SIM_RTOS_EVT_HTTP_NEW_STATE, &notifEvent, timeout) == AT_OK) {
    if (!SIM_BITS_IS(notifEvent, SIM_RTOS_EVT_HTTP_NEW_STATE)) goto endCmd;

    switch (hsimHttp->state) {
    case SIM_HTTP_STATE_GET_RESP:
      if (AT_Command(&hsim->atCmd, "+HTTPHEAD", 0, 0, 0, 0) != AT_OK) goto endCmd;
      if (resp->contentLen > 0) {
        goto readContent;
      }
      hsimHttp->state = SIM_HTTP_STATE_GET_BUF_CONTENT;
      hsim->rtos.eventSet(SIM_RTOS_EVT_HTTP_NEW_STATE);
      break;

    case SIM_HTTP_STATE_GET_BUF_CONTENT:
      if (resp->onGetData) {
        resp->onGetData(resp->contentBuffer, hsimHttp->contentBufLen);
      }
      if (resp->contentLen - hsimHttp->contentReadLen > 0) {
        goto readContent;
      }

    case SIM_HTTP_STATE_DONE:
      status = SIM_OK;
      goto endCmd;
      break;
    }

    continue;

  readContent:
    hsimHttp->state = SIM_HTTP_STATE_READING_CONTENT;

    uint16_t remainingLen = resp->contentLen - hsimHttp->contentReadLen;
    AT_DataSetNumber(&paramData[0], 0);
    AT_DataSetNumber(&paramData[1],
                     (remainingLen > resp->contentBufferSize)?
                         resp->contentBufferSize: remainingLen);
    if (AT_Command(&hsim->atCmd, "+HTTPREAD", 2, paramData, 0, 0) != AT_OK) goto endCmd;
  }


endCmd:
  if (hsimHttp->state > SIM_HTTP_STATE_STARTING) {
    AT_Command(&hsim->atCmd, "+HTTPTERM", 0, 0, 0, 0);
  }
  hsimHttp->state = SIM_HTTP_STATE_AVAILABLE;
  return status;
}

static void onGetResponse(void *app, AT_Data_t *resp)
{
  SIM_HandlerTypeDef *hsim = (SIM_HandlerTypeDef*)app;

  if (hsim->http.request == 0 || hsim->http.response == 0) return;
  if (resp->value.number != hsim->http.request->method) return;

  resp++;
  hsim->http.response->code = resp->value.number;

  resp++;
  hsim->http.response->contentLen = resp->value.number;
  hsim->http.state = SIM_HTTP_STATE_GET_RESP;
  hsim->rtos.eventSet(SIM_RTOS_EVT_HTTP_NEW_STATE);
}


static struct AT_BufferReadTo onReadHead(void *app, AT_Data_t *data)
{
  SIM_HandlerTypeDef *hsim = (SIM_HandlerTypeDef*)app;
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
  SIM_HandlerTypeDef *hsim = (SIM_HandlerTypeDef*)app;
  struct AT_BufferReadTo returnBuf = {
      .buffer = 0,
      .bufferSize = 0,
      .readLen = 0,
  };

  char *flag = resp->value.string;

  if (resp->type == AT_NUMBER && resp->value.number == 0)
  {
    hsim->http.state = SIM_HTTP_STATE_GET_BUF_CONTENT;
    hsim->rtos.eventSet(SIM_RTOS_EVT_HTTP_NEW_STATE);
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



#endif /* SIM_EN_FEATURE_HTTP */
