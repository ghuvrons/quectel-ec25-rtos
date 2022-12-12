/*
 * file.h
 *
 *  Created on: Nov 23, 2022
 *      Author: janoko
 */

#ifndef SIMCOM_7600E_FILE_H_
#define SIMCOM_7600E_FILE_H_

#include "conf.h"
#if SIM_EN_FEATURE_FILE

#include "types.h"

typedef struct {
  void      *hsim;
  uint32_t  memoryTotal;
  uint32_t  memoryUsed;
} SIM_FILE_HandlerTypeDef;


SIM_Status_t SIM_FILE_Init(SIM_FILE_HandlerTypeDef*, void *hsim);

SIM_Status_t SIM_FILE_ChangeDir(SIM_FILE_HandlerTypeDef*, const char *dir);
SIM_Status_t SIM_FILE_MakeDir(SIM_FILE_HandlerTypeDef*, const char *dir);
SIM_Status_t SIM_FILE_RemoveDir(SIM_FILE_HandlerTypeDef*, const char *dir);
SIM_Status_t SIM_FILE_ListDir(SIM_FILE_HandlerTypeDef*, const char *dir);
SIM_Status_t SIM_FILE_MemoryInfo(SIM_FILE_HandlerTypeDef*);

SIM_Status_t SIM_FILE_IsFileExist(SIM_FILE_HandlerTypeDef*, const char *filepath);
SIM_Status_t SIM_FILE_CreateAndWriteFile(SIM_FILE_HandlerTypeDef*, const char *filepath, uint8_t* data, uint16_t len);
SIM_Status_t SIM_FILE_CopyFile(SIM_FILE_HandlerTypeDef*, const char *filepath1, const char *filepath2);
SIM_Status_t SIM_FILE_RenameFile(SIM_FILE_HandlerTypeDef*, const char *filepath, const char *newName);
SIM_Status_t SIM_FILE_RemoveFile(SIM_FILE_HandlerTypeDef*, const char *filepath);

#endif /* SIM_EN_FEATURE_FILE */
#endif /* SIMCOM_7600E_FILE_H_ */
