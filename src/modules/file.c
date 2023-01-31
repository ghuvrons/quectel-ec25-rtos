/*
 * file.c
 *
 *  Created on: Nov 23, 2022
 *      Author: janoko
 */

#include <quectel-ec25/file.h>

#if QTEL_EN_FEATURE_FILE
#include <quectel-ec25.h>
#include "../events.h"
#include <quectel-ec25/debug.h>
#include <at-command/utils.h>
#include <string.h>


QTEL_Status_t QTEL_FILE_Init(QTEL_FILE_HandlerTypeDef *qtelFile, void *qtelPtr)
{
  if (((QTEL_HandlerTypeDef*)qtelPtr)->key != QTEL_KEY)
    return QTEL_ERROR;

  qtelFile->qtel = qtelPtr;

  return QTEL_OK;
}


QTEL_Status_t QTEL_FILE_MemoryInfo(QTEL_FILE_HandlerTypeDef *qtelFile)
{
//  QTEL_HandlerTypeDef *qtel = qtelFile->qtel;
//
//  uint8_t       respBuf[64];
//  const uint8_t *respBufPtr = respBuf;
//  AT_Data_t     respData[1] = {
//    AT_Buffer(respBuf, 64),
//  };
//
//  AT_Data_t memTotal = AT_Number(0);
//  AT_Data_t memUsed = AT_Number(0);
//
//  if (AT_Command(&qtel->atCmd, "+FSMEM", 0, 0, 1, respData) != AT_OK) return QTEL_ERROR;
//
//  while (*respBufPtr != 0) {
//    if (*respBufPtr == '(') {
//      respBufPtr++;
//      break;
//    }
//
//    respBufPtr++;
//  }
//
//  respBufPtr = (const uint8_t*) AT_ParseResponse((const char*)respBufPtr, &memTotal);
//  respBufPtr = (const uint8_t*) AT_ParseResponse((const char*)respBufPtr, &memUsed);
//
//  qtelFile->memoryTotal = (uint32_t) memTotal.value.number;
//  qtelFile->memoryUsed = (uint32_t) memUsed.value.number;

  return QTEL_OK;
}


QTEL_Status_t QTEL_FILE_IsFileExist(QTEL_FILE_HandlerTypeDef *qtelFile, const char *filepath)
{
  return QTEL_OK;
}

QTEL_Status_t QTEL_FILE_CreateAndWriteFile(QTEL_FILE_HandlerTypeDef *qtelFile,
                                           const char *filepath,
                                           const uint8_t* data, uint16_t len)
{
  return QTEL_OK;
}

QTEL_Status_t QTEL_FILE_RemoveFile(QTEL_FILE_HandlerTypeDef *qtelFile, const char *filepath)
{
  QTEL_HandlerTypeDef *qtel = qtelFile->qtel;

  AT_Data_t paramData[1] = {
      AT_Bytes(filepath, strlen(filepath)),
  };

  if (AT_Command(&qtel->atCmd, "+FSDEL", 1, paramData, 0, 0) != AT_OK) return QTEL_ERROR;

  return QTEL_OK;
}


QTEL_Status_t QTEL_FILE_Open(QTEL_FILE_HandlerTypeDef *qtelFile, const char *filename, int *fn)
{
  QTEL_HandlerTypeDef *qtel = qtelFile->qtel;

  AT_Data_t paramData[1] = {
    AT_String(filename),
  };

  AT_Data_t respData[1] = {
    AT_Number(0),
  };

  if (AT_Command(&qtel->atCmd, "+QFOPEN", 1, paramData, 1, respData) != AT_OK)
    return QTEL_ERROR;

  *fn = respData[0].value.number;

  return QTEL_OK;
}


int QTEL_FILE_Read(QTEL_FILE_HandlerTypeDef *qtelFile, int fn, void *buffer, uint32_t length)
{
  QTEL_HandlerTypeDef *qtel = qtelFile->qtel;
  AT_HandlerTypeDef *hat = &qtel->atCmd;

  int readLength = -1;
  uint16_t writecmdLen;
  const char *cmd = "+QFREAD";
  AT_Data_t paramData[2] = {
    AT_Number(fn),
    AT_Number(length)
  };
  AT_Data_t respData[1] = {
    AT_Number(0),
  };

  if (hat->rtos.mutexLock(hat->config.commandTimeout) != AT_OK) return -1;
  hat->rtos.eventClear(AT_EVT_OK|AT_EVT_ERROR|AT_EVT_CMD_RESP);
  qtel->rtos.eventClear(QTEL_RTOS_EVT_RESP_CONNECT);
  qtel->isRespConnectHandle = 1;

  writecmdLen = AT_WriteCommand(hat->bufferCmd, AT_BUF_CMD_SZ, cmd, (length == 0)?1:2, paramData);

  hat->currentCommand.cmdLen  = strlen(cmd);
  hat->currentCommand.cmd     = cmd;
  hat->currentCommand.respNb  = 1;
  hat->currentCommand.resp    = respData;

  hat->serial.write(hat->bufferCmd, writecmdLen);

  // wait response
  uint32_t events;

  // wait onGetRespConnect executed
  if (qtel->rtos.eventWait(QTEL_RTOS_EVT_RESP_CONNECT, &events, hat->config.commandTimeout) != AT_OK) {
    qtel->rtos.eventSet(QTEL_RTOS_EVT_RESP_CONNECT_CLOSE);
    qtel->isRespConnectHandle = 0;
    goto endCmd;
  }

  readLength = (uint32_t) atoi((char*) &qtel->respConnectBuffer[8]);

  hat->serial.readinto(buffer, readLength);

  // set event into func onGetRespConnect
  qtel->rtos.eventSet(QTEL_RTOS_EVT_RESP_CONNECT_CLOSE);
  qtel->isRespConnectHandle = 0;

  if (hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, hat->config.commandTimeout) != AT_OK) {
    goto endCmd;
  }

  if (events & AT_EVT_ERROR) {
    goto endCmd;
  }

  events = 0;
  hat->rtos.eventWait(AT_EVT_CMD_RESP, &events, 1);

endCmd:
  memset(&hat->currentCommand, 0, sizeof(hat->currentCommand));
  hat->rtos.mutexUnlock();
  return readLength;
}


int QTEL_FILE_Write(QTEL_FILE_HandlerTypeDef *qtelFile, int fn,
                              const uint8_t *data, uint32_t length)
{
  QTEL_HandlerTypeDef *qtel = qtelFile->qtel;

  AT_Data_t paramData[2] = {
    AT_Number(fn),
    AT_Number(length),
  };

  AT_Data_t respData[2] = {
    AT_Number(0),
    AT_Number(0),
  };


  if (AT_CommandWrite(&qtel->atCmd, "+QFWRITE", "CONNECT\r\n", 0,
                      data, length,
                      2, paramData, 2, respData) != AT_OK)
  {
    return QTEL_ERROR;
  }

  return QTEL_OK;
}


QTEL_Status_t QTEL_FILE_Seek(QTEL_FILE_HandlerTypeDef *qtelFile, int fn, uint32_t pos)
{
  return QTEL_OK;
}


QTEL_Status_t QTEL_FILE_Close(QTEL_FILE_HandlerTypeDef *qtelFile, int fn)
{
  QTEL_HandlerTypeDef *qtel = qtelFile->qtel;

  AT_Data_t paramData[1] = {
    AT_Number(fn),
  };

  if (AT_Command(&qtel->atCmd, "+QFCLOSE", 1, paramData, 0, 0) != AT_OK)
    return QTEL_ERROR;

  return QTEL_OK;
}

#endif /* QTEL_EN_FEATURE_FILE */
