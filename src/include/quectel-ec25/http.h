/*

 * http.h
 *
 *  Created on: Nov 16, 2022
 *      Author: janoko
 */

#ifndef QUECTEL_EC25_HTTP_H_
#define QUECTEL_EC25_HTTP_H_

#include <quectel-ec25/conf.h>
#if QTEL_EN_FEATURE_HTTP

#include <quectel-ec25/types.h>

enum {
  QTEL_HTTP_STATE_AVAILABLE,
  QTEL_HTTP_STATE_STARTING,
  QTEL_HTTP_STATE_REQUESTING,
  QTEL_HTTP_STATE_GET_RESP,
  QTEL_HTTP_STATE_READING_CONTENT,
  QTEL_HTTP_STATE_GET_BUF_CONTENT,
  QTEL_HTTP_STATE_DONE,
};

typedef struct {
  char*         url;
  uint8_t       method;
  const uint8_t *httpData;       // header + content
  uint16_t      httpDataLength;
} QTEL_HTTP_Request_t;

typedef struct {
  // set by user
  void      *headBuffer;                 // optional for buffer head
  uint16_t  headBufferSize;          // optional for size of buffer head
  void      *contentBuffer;              // optional for buffer data
  uint16_t  contentBufferSize;       // optional for size of buffer data
  void      (*onGetData)(void *contentBuffer, uint16_t len);

  // set by simcom
  uint8_t   status;
  uint8_t   err;
  uint16_t  code;
  uint16_t  contentLen;
} QTEL_HTTP_Response_t;


typedef struct {
  void      *hsim;
  uint8_t   state;
  uint32_t  stateTick;
  uint8_t   events;

  uint16_t contentBufLen;        // length of buffer which is available to handle
  uint16_t contentReadLen;

  QTEL_HTTP_Request_t  *request;
  QTEL_HTTP_Response_t *response;
} QTEL_HTTP_HandlerTypeDef;


QTEL_Status_t QTEL_HTTP_Init(QTEL_HTTP_HandlerTypeDef*, void *hsim);
QTEL_Status_t QTEL_HTTP_Get(QTEL_HTTP_HandlerTypeDef*, char *url, QTEL_HTTP_Response_t*, uint32_t timeout);
QTEL_Status_t QTEL_HTTP_SendRequest(QTEL_HTTP_HandlerTypeDef *hsimHttp, char *url,
                                    uint8_t method,
                                    const uint8_t *httpRequest,
                                    uint16_t httpRequestLength,
                                    QTEL_HTTP_Response_t *resp,
                                    uint32_t timeout);

#endif /* QTEL_EN_FEATURE_HTTP */
#endif /* QUECTEL_EC25_HTTP_H_ */
