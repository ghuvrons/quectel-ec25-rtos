/*
 * http.h
 *
 *  Created on: Nov 16, 2022
 *      Author: janoko
 */

#ifndef SIMCOM_7600E_HTTP_H_
#define SIMCOM_7600E_HTTP_H_

#include "conf.h"
#if SIM_EN_FEATURE_HTTP

#include "types.h"

enum {
  SIM_HTTP_STATE_AVAILABLE,
  SIM_HTTP_STATE_STARTING,
  SIM_HTTP_STATE_REQUESTING,
  SIM_HTTP_STATE_GET_RESP,
  SIM_HTTP_STATE_READING_CONTENT,
  SIM_HTTP_STATE_GET_BUF_CONTENT,
  SIM_HTTP_STATE_DONE,
};

typedef struct {
  char*         url;
  uint8_t       method;
  const uint8_t *httpData;       // header + content
  uint16_t      httpDataLength;
} SIM_HTTP_Request_t;

typedef struct {
  // set by user
  void *headBuffer;                 // optional for buffer head
  uint16_t headBufferSize;          // optional for size of buffer head
  void *contentBuffer;              // optional for buffer data
  uint16_t contentBufferSize;       // optional for size of buffer data
  void (*onGetData)(void *contentBuffer, uint16_t len);

  // set by simcom
  uint8_t status;
  uint8_t err;
  uint16_t code;
  uint16_t contentLen;
} SIM_HTTP_Response_t;


typedef struct {
  void      *hsim;
  uint8_t   state;
  uint32_t  stateTick;
  uint8_t   events;

  uint16_t contentBufLen;        // length of buffer which is available to handle
  uint16_t contentReadLen;

  SIM_HTTP_Request_t  *request;
  SIM_HTTP_Response_t *response;
} SIM_HTTP_HandlerTypeDef;


SIM_Status_t SIM_HTTP_Init(SIM_HTTP_HandlerTypeDef*, void *hsim);
SIM_Status_t SIM_HTTP_Get(SIM_HTTP_HandlerTypeDef*, char *url, SIM_HTTP_Response_t*, uint32_t timeout);
SIM_Status_t SIM_HTTP_SendRequest(SIM_HTTP_HandlerTypeDef *hsimHttp, char *url,
                                  uint8_t method,
                                  const uint8_t *httpRequest,
                                  uint16_t httpRequestLength,
                                  SIM_HTTP_Response_t *resp,
                                  uint32_t timeout);
#endif /* SIM_EN_FEATURE_HTTP */
#endif /* SIMCOM_7600E_HTTP_H_ */
