/*

 * file.h
 *
 *  Created on: Nov 23, 2022
 *      Author: janoko
 */

#ifndef QUECTEL_EC25_FILE_H_
#define QUECTEL_EC25_FILE_H_

#include <quectel-ec25/conf.h>
#if QTEL_EN_FEATURE_FILE

#include <quectel-ec25/types.h>

typedef struct {
  void      *qtel;
  uint32_t  memoryTotal;
  uint32_t  memoryUsed;
} QTEL_FILE_HandlerTypeDef;


QTEL_Status_t QTEL_FILE_Init(QTEL_FILE_HandlerTypeDef*, void *qtelPtr);

QTEL_Status_t QTEL_FILE_ChangeDir(QTEL_FILE_HandlerTypeDef*, const char *dir);
QTEL_Status_t QTEL_FILE_MakeDir(QTEL_FILE_HandlerTypeDef*, const char *dir);
QTEL_Status_t QTEL_FILE_RemoveDir(QTEL_FILE_HandlerTypeDef*, const char *dir);
QTEL_Status_t QTEL_FILE_ListDir(QTEL_FILE_HandlerTypeDef*, const char *dir);
QTEL_Status_t QTEL_FILE_MemoryInfo(QTEL_FILE_HandlerTypeDef*);

QTEL_Status_t QTEL_FILE_IsFileExist(QTEL_FILE_HandlerTypeDef*, const char *filepath);
QTEL_Status_t QTEL_FILE_CreateAndWriteFile(QTEL_FILE_HandlerTypeDef*, const char *filepath, uint8_t* data, uint16_t len);
QTEL_Status_t QTEL_FILE_CopyFile(QTEL_FILE_HandlerTypeDef*, const char *filepath1, const char *filepath2);
QTEL_Status_t QTEL_FILE_RenameFile(QTEL_FILE_HandlerTypeDef*, const char *filepath, const char *newName);
QTEL_Status_t QTEL_FILE_RemoveFile(QTEL_FILE_HandlerTypeDef*, const char *filepath);

#endif /* QTEL_EN_FEATURE_FILE */
#endif /* QUECTEL_EC25_FILE_H_ */
