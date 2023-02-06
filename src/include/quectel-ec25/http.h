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

#define QTEL_HTTP_STATUS_CONFIGURED 0x01


enum {
  QTEL_HTTP_STATE_NON_ACTIVE,
  QTEL_HTTP_STATE_ACTIVATING,
  QTEL_HTTP_STATE_AVAILABLE,
  QTEL_HTTP_STATE_STARTING,
  QTEL_HTTP_STATE_REQUESTING,
  QTEL_HTTP_STATE_GET_RESP,
  QTEL_HTTP_STATE_TMP_FILE_READY,
  QTEL_HTTP_STATE_READING_CONTENT,
  QTEL_HTTP_STATE_GET_BUF_CONTENT,
  QTEL_HTTP_STATE_DONE,
};

typedef enum {
  QTEL_HTTP_GET,
  QTEL_HTTP_POST,
  QTEL_HTTP_METHOD_MAX,
} QTEL_HTTP_Method_t;

typedef struct {
  const char*   url;
  uint8_t       method;
  uint8_t       isWithHeader;
  const uint8_t *httpData;         // content
  uint16_t      httpDataLength;

  const char    *saveto;
} QTEL_HTTP_Request_t;

typedef struct {
  // set by user
  void        *headerBuffer;            // optional for buffer head
  uint16_t    headerBufferSize;         // optional for size of buffer head
  void        *contentBuffer;           // optional for buffer data
  uint16_t    contentBufferSize;        // optional for size of buffer data
  void        (*onGetData)(void *contentBuffer, uint16_t len);

  // set by simcom
  uint8_t   status;
  uint16_t  err;
  uint16_t  code;
  uint32_t  contentLen;
} QTEL_HTTP_Response_t;


typedef struct {
  void      *qtel;
  uint8_t   contextId;
  uint8_t   status;
  uint8_t   state;
  uint32_t  stateTick;
  uint8_t   events;

  uint16_t contentBufLen;        // length of buffer which is available to handle
  uint16_t contentReadLen;

  QTEL_HTTP_Request_t  *request;
  QTEL_HTTP_Response_t *response;
} QTEL_HTTP_HandlerTypeDef;


QTEL_Status_t QTEL_HTTP_Init(QTEL_HTTP_HandlerTypeDef*, void *hsim);

QTEL_Status_t QTEL_HTTP_SendRequest(QTEL_HTTP_HandlerTypeDef *hsimHttp,
                                    QTEL_HTTP_Request_t *req,
                                    QTEL_HTTP_Response_t *resp,
                                    uint32_t timeout);

QTEL_Status_t QTEL_HTTP_Get(QTEL_HTTP_HandlerTypeDef*,
                            const char *url,
                            QTEL_HTTP_Response_t*,
                            uint32_t timeout);

QTEL_Status_t QTEL_HTTP_DownloadAndSave(QTEL_HTTP_HandlerTypeDef*,
                                        const char *url,
                                        const char *filename,
                                        QTEL_HTTP_Response_t*,
                                        uint32_t timeout);

#endif /* QTEL_EN_FEATURE_HTTP */
#endif /* QUECTEL_EC25_HTTP_H_ */
