/*
 * file.c
 *
 *  Created on: Nov 23, 2022
 *      Author: janoko
 */

#include "../include/simcom/file.h"

#if SIM_EN_FEATURE_FILE
#include "../include/simcom.h"
#include "../include/simcom/debug.h"
#include <at-command/utils.h>
#include <string.h>


SIM_Status_t SIM_FILE_Init(SIM_FILE_HandlerTypeDef *hsimFile, void *hsim)
{
  if (((SIM_HandlerTypeDef*)hsim)->key != SIM_KEY)
    return SIM_ERROR;

  hsimFile->hsim = hsim;

  return SIM_OK;
}


SIM_Status_t SIM_FILE_ChangeDir(SIM_FILE_HandlerTypeDef *hsimFile, const char *dir)
{
  SIM_HandlerTypeDef  *hsim       = hsimFile->hsim;

  AT_Data_t           paramData[1] = {
      AT_Bytes(dir, strlen(dir)),
  };

  if (AT_Command(&hsim->atCmd, "+FSCD", 1, paramData, 0, 0) != AT_OK) return SIM_ERROR;

  return SIM_OK;
}


SIM_Status_t SIM_FILE_MakeDir(SIM_FILE_HandlerTypeDef *hsimFile, const char *dir)
{
  SIM_HandlerTypeDef  *hsim       = hsimFile->hsim;

  AT_Data_t           paramData[1] = {
      AT_Bytes(dir, strlen(dir)),
  };

  if (AT_Command(&hsim->atCmd, "+FSMKDIR", 1, paramData, 0, 0) != AT_OK) return SIM_ERROR;

  return SIM_OK;
}


SIM_Status_t SIM_FILE_MemoryInfo(SIM_FILE_HandlerTypeDef *hsimFile)
{
  SIM_HandlerTypeDef  *hsim       = hsimFile->hsim;

  uint8_t       respBuf[64];
  const uint8_t *respBufPtr = respBuf;
  AT_Data_t     respData[1] = {
    AT_Buffer(respBuf, 64),
  };

  AT_Data_t memTotal = AT_Number(0);
  AT_Data_t memUsed = AT_Number(0);

  if (AT_Command(&hsim->atCmd, "+FSMEM", 0, 0, 1, respData) != AT_OK) return SIM_ERROR;

  while (*respBufPtr != 0) {
    if (*respBufPtr == '(') {
      respBufPtr++;
      break;
    }

    respBufPtr++;
  }

  respBufPtr = (const uint8_t*) AT_ParseResponse((const char*)respBufPtr, &memTotal);
  respBufPtr = (const uint8_t*) AT_ParseResponse((const char*)respBufPtr, &memUsed);

  hsimFile->memoryTotal = (uint32_t) memTotal.value.number;
  hsimFile->memoryUsed = (uint32_t) memUsed.value.number;


  return SIM_OK;
}


SIM_Status_t SIM_FILE_IsFileExist(SIM_FILE_HandlerTypeDef *hsimFile, const char *filepath)
{
  SIM_HandlerTypeDef  *hsim       = hsimFile->hsim;

  AT_Data_t           paramData[3] = {
      AT_String(filepath),
      AT_Number(0),
      AT_Number(1),
  };

  if (AT_Command(&hsim->atCmd, "+CFTRANTX", 3, paramData, 0, 0) != AT_OK) return SIM_ERROR;

  return SIM_OK;
}

SIM_Status_t SIM_FILE_CreateAndWriteFile(SIM_FILE_HandlerTypeDef *hsimFile,
                                         const char *filepath,
                                         uint8_t* data, uint16_t len)
{
  SIM_HandlerTypeDef  *hsim       = hsimFile->hsim;

  AT_Data_t paramData[2] = {
      AT_String(filepath),
      AT_Number(len),
  };

  if (AT_CommandWrite(&hsim->atCmd, "+CFTRANRX", ">",
                      data, len,
                      2, paramData, 0, 0) != AT_OK)
  {
    return SIM_ERROR;
  }

  return SIM_OK;
}

SIM_Status_t SIM_FILE_RemoveFile(SIM_FILE_HandlerTypeDef *hsimFile, const char *filepath)
{
  SIM_HandlerTypeDef  *hsim       = hsimFile->hsim;

  AT_Data_t paramData[1] = {
      AT_Bytes(filepath, strlen(filepath)),
  };

  if (AT_Command(&hsim->atCmd, "+FSDEL", 1, paramData, 0, 0) != AT_OK) return SIM_ERROR;

  return SIM_OK;
}

#endif /* SIM_EN_FEATURE_FILE */
