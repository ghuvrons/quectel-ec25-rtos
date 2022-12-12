/*
 * modem-rtos.h
 *
 *  Created on: Nov 2, 2022
 *      Author: janoko
 */

#ifndef MODULES_MODEM_RTOS_H_
#define MODULES_MODEM_RTOS_H_

#include <os.h>

#ifndef MODEM_STACK_SZ
#define MODEM_STACK_SZ 512
#endif

#ifndef SIMCOM_STACK_SZ
#define SIMCOM_STACK_SZ 2048
#endif


#if USE_FREERTOS
void MDM_Task_Init(void);

#elif USE_THREADX
void MDM_Thread_Init(VOID *memory_ptr);
#endif

void MDM_PrintRTOSInfo(void);

#endif /* MODULES_MODEM_RTOS_H_ */
