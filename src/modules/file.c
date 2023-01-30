/*
 * file.c
 *
 *  Created on: Nov 23, 2022
 *      Author: janoko
 */

#include <quectel-ec25/file.h>

#if QTEL_EN_FEATURE_FILE
#include <quectel-ec25.h>
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


QTEL_Status_t QTEL_FILE_ChangeDir(QTEL_FILE_HandlerTypeDef *qtelFile, const char *dir)
{
  QTEL_HandlerTypeDef *qtel = qtelFile->qtel;

  AT_Data_t paramData[1] = {
      AT_Bytes(dir, strlen(dir)),
  };

  if (AT_Command(&qtel->atCmd, "+FSCD", 1, paramData, 0, 0) != AT_OK) return QTEL_ERROR;

  return QTEL_OK;
}


QTEL_Status_t QTEL_FILE_MakeDir(QTEL_FILE_HandlerTypeDef *qtelFile, const char *dir)
{
  QTEL_HandlerTypeDef *qtel = qtelFile->qtel;

  AT_Data_t paramData[1] = {
      AT_Bytes(dir, strlen(dir)),
  };

  if (AT_Command(&qtel->atCmd, "+FSMKDIR", 1, paramData, 0, 0) != AT_OK) return QTEL_ERROR;

  return QTEL_OK;
}


QTEL_Status_t QTEL_FILE_MemoryInfo(QTEL_FILE_HandlerTypeDef *qtelFile)
{
  QTEL_HandlerTypeDef *qtel = qtelFile->qtel;

  uint8_t       respBuf[64];
  const uint8_t *respBufPtr = respBuf;
  AT_Data_t     respData[1] = {
    AT_Buffer(respBuf, 64),
  };

  AT_Data_t memTotal = AT_Number(0);
  AT_Data_t memUsed = AT_Number(0);

  if (AT_Command(&qtel->atCmd, "+FSMEM", 0, 0, 1, respData) != AT_OK) return QTEL_ERROR;

  while (*respBufPtr != 0) {
    if (*respBufPtr == '(') {
      respBufPtr++;
      break;
    }

    respBufPtr++;
  }

  respBufPtr = (const uint8_t*) AT_ParseResponse((const char*)respBufPtr, &memTotal);
  respBufPtr = (const uint8_t*) AT_ParseResponse((const char*)respBufPtr, &memUsed);

  qtelFile->memoryTotal = (uint32_t) memTotal.value.number;
  qtelFile->memoryUsed = (uint32_t) memUsed.value.number;


  return QTEL_OK;
}


QTEL_Status_t QTEL_FILE_IsFileExist(QTEL_FILE_HandlerTypeDef *qtelFile, const char *filepath)
{
  QTEL_HandlerTypeDef *qtel = qtelFile->qtel;

  AT_Data_t paramData[3] = {
      AT_String(filepath),
      AT_Number(0),
      AT_Number(1),
  };

  if (AT_Command(&qtel->atCmd, "+CFTRANTX", 3, paramData, 0, 0) != AT_OK) return QTEL_ERROR;

  return QTEL_OK;
}

QTEL_Status_t QTEL_FILE_CreateAndWriteFile(QTEL_FILE_HandlerTypeDef *qtelFile,
                                           const char *filepath,
                                           const uint8_t* data, uint16_t len)
{
  QTEL_HandlerTypeDef *qtel = qtelFile->qtel;

  AT_Data_t paramData[2] = {
      AT_String(filepath),
      AT_Number(len),
  };

  if (AT_CommandWrite(&qtel->atCmd, "+CFTRANRX", ">", 0,
                      data, len,
                      2, paramData, 0, 0) != AT_OK)
  {
    return QTEL_ERROR;
  }

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

#endif /* QTEL_EN_FEATURE_FILE */
