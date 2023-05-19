/*
 * http.c
 *
 *  Created on: Nov 16, 2022
 *      Author: janoko
 */

#include <quectel-ec25/http.h>

#if QTEL_EN_FEATURE_HTTP
#include "../events.h"
#include <quectel-ec25.h>
#include <quectel-ec25/file.h>
#include <quectel-ec25/utils.h>
#include <string.h>
#include <stdlib.h>

#define FILENAME_TMP "RAM:modem_tmp.http"

static QTEL_Status_t httpConfigure(QTEL_HTTP_HandlerTypeDef*);

static void onGetResponse(void *app, AT_Data_t *resp);
static void onReadIntoFileDone(void *app, AT_Data_t *resp);


QTEL_Status_t QTEL_HTTP_Init(QTEL_HTTP_HandlerTypeDef *qtelhttp, void *qtelPtr)
{
  if (((QTEL_HandlerTypeDef*)qtelPtr)->key != QTEL_KEY)
    return QTEL_ERROR;

  qtelhttp->qtel = qtelPtr;
  qtelhttp->state = QTEL_HTTP_STATE_NON_ACTIVE;
  qtelhttp->stateTick = 0;
  qtelhttp->contextId = 2;

  AT_Data_t *httpActionResp = malloc(sizeof(AT_Data_t)*3);
  memset(httpActionResp, 0, sizeof(AT_Data_t)*3);
  AT_DataSetNumber(httpActionResp, 0);
  AT_DataSetNumber(httpActionResp+1, 0);
  AT_DataSetNumber(httpActionResp+2, 0);
  AT_On(&((QTEL_HandlerTypeDef*)qtelhttp->qtel)->atCmd, "+QHTTPGET",
        qtelhttp->qtel, 3, httpActionResp, onGetResponse);
  AT_On(&((QTEL_HandlerTypeDef*)qtelhttp->qtel)->atCmd, "+QHTTPREADFILE",
        qtelhttp->qtel, 1, httpActionResp, onReadIntoFileDone);

  return QTEL_OK;
}


QTEL_Status_t QTEL_HTTP_SendRequest(QTEL_HTTP_HandlerTypeDef *qtelhttp,
                                    QTEL_HTTP_Request_t *req,
                                    QTEL_HTTP_Response_t *resp,
                                    uint32_t timeout)
{
  QTEL_HandlerTypeDef *qtelPtr    = qtelhttp->qtel;
  QTEL_Status_t       status      = QTEL_ERROR;
  int                 fn          = -1;  // file number
  int                 tmpReadLen  = -1;
  uint32_t            contentRead = 0;
  uint32_t            notifEvent;
  AT_Data_t           paramData[3];

  if (req->url == 0 ||
      req->method >= QTEL_HTTP_METHOD_MAX)
  {
    return QTEL_ERROR;
  }

  if (qtelhttp->state == QTEL_HTTP_STATE_NON_ACTIVE) {
    qtelhttp->state = QTEL_HTTP_STATE_ACTIVATING;
    if (QTEL_NET_ConfigureContext(&qtelPtr->net, qtelhttp->contextId) != QTEL_OK)
    {
      qtelhttp->state = QTEL_HTTP_STATE_NON_ACTIVE;
      return QTEL_ERROR;
    }
    if (QTEL_NET_ActivateContext(&qtelPtr->net, qtelhttp->contextId) != QTEL_OK)
    {
      qtelhttp->state = QTEL_HTTP_STATE_NON_ACTIVE;
      return QTEL_ERROR;
    }
    qtelhttp->state = QTEL_HTTP_STATE_AVAILABLE;
  }

  uint32_t httpTick = qtelPtr->getTick();
  while (qtelhttp->state != QTEL_HTTP_STATE_AVAILABLE) {
    if (QTEL_IsTimeout(qtelPtr, httpTick, timeout)) {
      return QTEL_TIMEOUT;
    }
    qtelPtr->delay(1);
  }

  qtelhttp->state = QTEL_HTTP_STATE_STARTING;

  resp->status       = 0;
  resp->err          = 0;
  resp->code         = 0;
  resp->contentLen   = 0;

  qtelhttp->request  = req;
  qtelhttp->response = resp;

  qtelhttp->contentBufLen = 0;
  qtelhttp->contentReadLen = 0;

  qtelPtr->rtos.eventClear(QTEL_RTOS_EVT_HTTP_NEW_STATE);

  status = httpConfigure(qtelhttp);
  if (status != QTEL_OK) goto endCmd;

  AT_DataSetString(&paramData[0], "requestheader");
  AT_DataSetNumber(&paramData[1], (req->isWithHeader)? 1: 0);
  if (AT_Command(&qtelPtr->atCmd, "+QHTTPCFG", 2, paramData, 0, 0) != AT_OK)
    goto endCmd;

  AT_DataSetNumber(&paramData[0], strlen(req->url));
  if (AT_CommandWrite(&qtelPtr->atCmd, "+QHTTPURL", "CONNECT", 0,
                      (const uint8_t*)req->url, strlen(req->url),
                      1, paramData, 0, 0) != AT_OK)
  {
    goto endCmd;
  }

  qtelPtr->rtos.eventClear(QTEL_RTOS_EVT_HTTP_NEW_STATE);

  switch (req->method) {
  case QTEL_HTTP_GET:
    AT_DataSetNumber(&paramData[0], timeout/1000);

    if (req->isWithHeader) {
      AT_DataSetNumber(&paramData[1], req->httpDataLength);
      if (AT_CommandWrite(&qtelPtr->atCmd, "+QHTTPGET", "CONNECT", 0,
                          req->httpData, req->httpDataLength,
                          2, paramData, 0, 0) != AT_OK)
      {
        goto endCmd;
      }
    } else {
      if (AT_Command(&qtelPtr->atCmd, "+QHTTPGET", 1, paramData, 0, 0) != AT_OK)
        goto endCmd;
    }
    break;

  default: goto endCmd;
  }

  qtelhttp->state = QTEL_HTTP_STATE_REQUESTING;

  while (qtelPtr->rtos.eventWait(QTEL_RTOS_EVT_HTTP_NEW_STATE,
                                 &notifEvent,
                                 timeout) == AT_OK)
  {
    if (!QTEL_BITS_IS(notifEvent, QTEL_RTOS_EVT_HTTP_NEW_STATE)) goto endCmd;

    switch (qtelhttp->state) {
    case QTEL_HTTP_STATE_GET_RESP:
      // put response data to temporary file
      AT_DataSetString(&paramData[0], (req->saveto == 0)? FILENAME_TMP: req->saveto);
      AT_DataSetNumber(&paramData[1], 100); // timeout
      if (AT_Command(&qtelPtr->atCmd, "+QHTTPREADFILE", 2, paramData, 0, 0) != AT_OK)
        goto endCmd;
      break;

    case QTEL_HTTP_STATE_TMP_FILE_READY:
      if (resp->err != 0) goto endCmd;
      if (req->saveto != 0) goto endCmd;

      if (resp->contentBuffer == 0 ||
          resp->contentBufferSize == 0)
      {
        goto endCmd;
      }
      QTEL_FILE_Open(&qtelPtr->file, FILENAME_TMP, &fn);
      qtelhttp->state = QTEL_HTTP_STATE_READING_CONTENT;
      while (contentRead < resp->contentLen) {
        tmpReadLen = QTEL_FILE_Read(&qtelPtr->file, fn, resp->contentBuffer, resp->contentBufferSize);
        if (tmpReadLen == -1) {
          goto endCmd;
        }
        if (resp->onGetData)
          resp->onGetData(resp->contentBuffer, (uint16_t) tmpReadLen);
        contentRead += tmpReadLen;
      }
      goto endCmd;
      break;
    }
  }

endCmd:
  if (fn != -1) {
    QTEL_FILE_Close(&qtelPtr->file, fn);
    QTEL_FILE_RemoveFile(&qtelPtr->file, FILENAME_TMP);
  }
  qtelhttp->state = QTEL_HTTP_STATE_AVAILABLE;
  return status;
}


QTEL_Status_t QTEL_HTTP_Get(QTEL_HTTP_HandlerTypeDef *qtelhttp,
                            const char *url,
                            QTEL_HTTP_Response_t *resp,
                            uint32_t timeout)
{
  QTEL_HTTP_Request_t  req;

  memset(&req, 0, sizeof(QTEL_HTTP_Request_t));

  req.url     = url;
  req.method  = QTEL_HTTP_GET;

  return QTEL_HTTP_SendRequest(qtelhttp, &req, resp, timeout);
}


QTEL_Status_t QTEL_HTTP_DownloadAndSave(QTEL_HTTP_HandlerTypeDef *qtelhttp,
                                        const char *url,
                                        const char *filename,
                                        QTEL_HTTP_Response_t *resp,
                                        uint32_t timeout)
{
  QTEL_HTTP_Request_t  req;

  memset(&req, 0, sizeof(QTEL_HTTP_Request_t));

  req.url     = url;
  req.method  = QTEL_HTTP_GET;
  req.saveto  = filename;

  return QTEL_HTTP_SendRequest(qtelhttp, &req, resp, timeout);
}


static QTEL_Status_t httpConfigure(QTEL_HTTP_HandlerTypeDef *qtelhttp)
{
  QTEL_HandlerTypeDef *qtelPtr    = qtelhttp->qtel;
  AT_Status_t         status      = QTEL_ERROR;
  AT_Data_t           paramData[3];

  if (QTEL_IS_STATUS(qtelhttp, QTEL_HTTP_STATUS_CONFIGURED)) {
    return QTEL_OK;
  }

  AT_DataSetString(&paramData[0], "contextid");
  AT_DataSetNumber(&paramData[1], qtelhttp->contextId);
  status = AT_Command(&qtelPtr->atCmd, "+QHTTPCFG", 2, paramData, 0, 0);
  if (status != AT_OK) return (QTEL_Status_t) status;

  AT_DataSetString(&paramData[0], "responseheader");
  AT_DataSetNumber(&paramData[1], 0);
  status = AT_Command(&qtelPtr->atCmd, "+QHTTPCFG", 2, paramData, 0, 0);
  if (status != AT_OK) return (QTEL_Status_t) status;

  QTEL_FILE_RemoveFile(&qtelPtr->file, FILENAME_TMP);

  QTEL_SET_STATUS(qtelhttp, QTEL_HTTP_STATUS_CONFIGURED);

  return QTEL_OK;
}


static void onGetResponse(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;

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


static void onReadIntoFileDone(void *app, AT_Data_t *resp)
{
  QTEL_HandlerTypeDef *hsim = (QTEL_HandlerTypeDef*)app;

  if (hsim->http.request == 0 || hsim->http.response == 0) return;

  hsim->http.response->err = resp->value.number;

  hsim->http.state = QTEL_HTTP_STATE_TMP_FILE_READY;
  hsim->rtos.eventSet(QTEL_RTOS_EVT_HTTP_NEW_STATE);
}


#endif /* QTEL_EN_FEATURE_HTTP */
