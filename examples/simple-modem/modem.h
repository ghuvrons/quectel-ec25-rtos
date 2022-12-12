/*
 * modem.h
 *
 *  Created on: Dec 12, 2022
 *      Author: janoko
 */

#ifndef QUECTEL_EC25_RTOS_EXAMPLES_SIMPLE_MODEM_H_
#define QUECTEL_EC25_RTOS_EXAMPLES_SIMPLE_MODEM_H_

#include <_defs/types.h>
#include <os.h>
#include <quectel-ec25.h>
#include <stdint.h>
#include <stdbool.h>

#define MDM_TX_BUFFER_SZ 256
#define MDM_RX_BUFFER_SZ 4096

typedef struct {
  uint32_t tick;
  uint32_t timeout;
} MDM_DelayConfig;

typedef struct {
  QTEL_HandlerTypeDef driver;
  struct {
    MDM_DelayConfig signalUpdate;
  } delayConfigs;
} MDM_HandlerTypedef;

extern MDM_HandlerTypedef Mod_Modem;

bool MDM_Init(void);
void MDM_Run(OS_TYPEDEF_THREAD_ARG);
void MDM_Simcom_Run(OS_TYPEDEF_THREAD_ARG);

bool MDM_Start(void);
Datetime_t MDM_GetTime(void);
void MDM_SendUSSD(const char*);
void MDM_Reset(void);
bool MDM_WaitReady(uint32_t timeout);
bool MDM_WaitOnline(uint32_t timeout);

#if QTEL_EN_FEATURE_GPS
bool MDM_IsGPSWeakSignal(void);
#endif


#endif /* QUECTEL_EC25_RTOS_EXAMPLES_SIMPLE_MODEM_H_ */
