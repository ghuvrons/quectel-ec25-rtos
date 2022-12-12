/*
 * modem-rtos.c
 *
 *  Created on: Nov 2, 2022
 *      Author: janoko
 */

#include "modem.h"
#include "modem-rtos.h"
#include <utils/debugger.h>


OS_Thread_t MDM_Thread;
OS_Event_t  MDM_Event;
OS_Mutex_t  MDM_Mutex;

OS_Thread_t SIMCOM_Thread;
OS_Event_t  SIMCOM_Event;

#if USE_FREERTOS
#include <FreeRTOS.h>

uint32_t            MDM_TaskBuffer[MODEM_STACK_SZ];
StaticTask_t        MDM_TaskControlBlock;
StaticEventGroup_t  MDM_EventControlBlock;
StaticSemaphore_t   MDM_MutexControlBlock;

uint32_t            SIMCOM_TaskBuffer[SIMCOM_STACK_SZ];
StaticTask_t        SIMCOM_TaskControlBlock;
StaticEventGroup_t  SIMCOM_EventControlBlock;

#elif USE_THREADX
TX_THREAD             TX_MDM_Thread;
UCHAR                 TX_MDM_ThreadStack[MODEM_STACK_SZ];
TX_EVENT_FLAGS_GROUP  TX_MDM_Event;
TX_MUTEX              TX_MDM_Mutex;

TX_THREAD             TX_SIMCOM_Thread;
UCHAR                 TX_SIMCOM_ThreadStack[SIMCOM_STACK_SZ];
TX_EVENT_FLAGS_GROUP  TX_SIMCOM_Event;
#endif /* USE_THREADX */

#if USE_FREERTOS
void MDM_Task_Init(void)
{
  const osThreadAttr_t MDM_Task_attr = {
    .name       = "Modem Task",
    .cb_mem     = &MDM_TaskControlBlock,
    .cb_size    = sizeof(MDM_TaskControlBlock),
    .stack_mem  = &MDM_TaskBuffer[0],
    .stack_size = sizeof(MDM_TaskBuffer),
    .priority   = (osPriority_t) osPriorityLow,
  };
  MDM_Thread = osThreadNew(MDM_Run, NULL, &MDM_Task_attr);

  const osEventFlagsAttr_t MDM_Event_attr = {
    .name     = "Modem Event",
    .cb_mem   = &MDM_EventControlBlock,
    .cb_size  = sizeof(MDM_EventControlBlock),
  };
  MDM_Event = osEventFlagsNew(&MDM_Event_attr);

  const osMutexAttr_t MDM_Mutex_attr = {
    .name     = "Modem Mutex",
    .cb_mem   = &MDM_MutexControlBlock,
    .cb_size  = sizeof(MDM_MutexControlBlock),
  };
  MDM_Mutex = osMutexNew(&MDM_Mutex_attr);

  const osThreadAttr_t SIMCOM_Task_attr = {
    .name       = "SIMCOM Task",
    .cb_mem     = &SIMCOM_TaskControlBlock,
    .cb_size    = sizeof(SIMCOM_TaskControlBlock),
    .stack_mem  = &SIMCOM_TaskBuffer[0],
    .stack_size = sizeof(SIMCOM_TaskBuffer),
    .priority   = (osPriority_t) osPriorityLow,
  };
  SIMCOM_Thread = osThreadNew(MDM_Simcom_Run, NULL, &SIMCOM_Task_attr);

  const osEventFlagsAttr_t SIMCOM_Event_attr = {
    .name     = "SIMCOM Event",
    .cb_mem   = &SIMCOM_EventControlBlock,
    .cb_size  = sizeof(SIMCOM_EventControlBlock),
  };
  SIMCOM_Event = osEventFlagsNew(&SIMCOM_Event_attr);
}

void MDM_PrintRTOSInfo(void)
{
  DBG_Println("========== Modem_INFO ============");
  DBG_Println("Modem: %u", osThreadGetStackSpace(MDM_Thread));
  DBG_Println("SIMCOM: %u", osThreadGetStackSpace(SIMCOM_Thread));
  DBG_Println("==================================");
}

//  DBG_Println("MqttTaskHandle: %u", osThreadGetStackSpace(MqttTaskHandle));

#elif USE_THREADX
void MDM_Thread_Init(VOID *memory_ptr)
{
  MDM_Thread = &TX_MDM_Thread;
  MDM_Event  = &TX_MDM_Event;
  MDM_Mutex  = &TX_MDM_Mutex;

  tx_thread_create(MDM_Thread, "Modem", MDM_Run, 0,
                   &TX_MDM_ThreadStack[0], MODEM_STACK_SZ, 15, 15, 1, TX_AUTO_START);

  tx_event_flags_create(MDM_Event,"Modem Event");
  tx_mutex_create(MDM_Mutex,  "Modem Mutex", TX_NO_INHERIT);

}
#endif
