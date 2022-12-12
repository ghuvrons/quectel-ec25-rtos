/*
 * modem.c
 *
 *  Created on: Dec 12, 2022
 *      Author: janoko
 */

#include "modem.h"
#include "modem-rtos.h"
#include <usart.h>
#include <os.h>
#include <dma_streamer.h>
#include <quectel-ec25.h>
#include <quectel-ec25/net.h>
#include <quectel-ec25/ntp.h>
#include <string.h>
#include <utils/debugger.h>


#define MDM_EVT_INIT_SUCCESS 0x0800U

MDM_HandlerTypedef Mod_Modem;

extern OS_Thread_t MDM_Thread;
extern OS_Event_t  MDM_Event;
extern OS_Mutex_t  MDM_Mutex;
extern OS_Event_t  SIMCOM_Event;

static bool     isEventReset;
static uint8_t  MDM_TxBuffer[MDM_TX_BUFFER_SZ];
static uint8_t  MDM_RxBuffer[MDM_RX_BUFFER_SZ];

static STRM_handlerTypeDef mdm_hdma_streamer = {
    .huart = &DEV_UART_MODEM
};


static int serialRead(uint8_t *dst, uint16_t sz);
static int serialReadline(uint8_t *dst, uint16_t sz);
static int serialReadinto(void *buffer, uint16_t sz);
static int serialWrite(uint8_t *src, uint16_t sz);

static AT_Status_t mutexLock(uint32_t timeout);
static AT_Status_t mutexUnlock(void);
static AT_Status_t eventSet(uint32_t events);
static AT_Status_t eventWait(uint32_t waitevents, uint32_t *onEvents, uint32_t timeout);
static AT_Status_t eventClear(uint32_t events);

#if QTEL_EN_FEATURE_NTP & RTC_ENABLE
static void onNTPSynced(QTEL_Datetime_t);
#endif

void QTEL_Printf(const char *format, ...)
{
  va_list args;

  va_start (args, format);
  DBG_vPrintf(format, args);
  va_end (args);
}

void QTEL_Println(const char *format, ...)
{
  va_list args;

  va_start (args, format);
  DBG_vPrintln(format, args);
  va_end (args);
}


bool MDM_Init(void)
{
  memset(&Mod_Modem, 0, sizeof(MDM_HandlerTypedef));

  // configure
  Mod_Modem.delayConfigs.signalUpdate.tick = OS_GetTick();
  Mod_Modem.delayConfigs.signalUpdate.timeout = 10000;

  // streamer init
  mdm_hdma_streamer.config.breakLine = STRM_BREAK_CRLF;
  STRM_Init(&mdm_hdma_streamer,
            &(MDM_TxBuffer[0]), MDM_TX_BUFFER_SZ,
            &(MDM_RxBuffer[0]), MDM_RX_BUFFER_SZ);
  OS_Delay(10);

  // init driver methods
  Mod_Modem.driver.delay = OS_Delay;
  Mod_Modem.driver.getTick = OS_GetTick;

  Mod_Modem.driver.serial.read = serialRead;
  Mod_Modem.driver.serial.readline = serialReadline;
  Mod_Modem.driver.serial.readinto = serialReadinto;
  Mod_Modem.driver.serial.write = serialWrite;

  Mod_Modem.driver.rtos.mutexLock = mutexLock;
  Mod_Modem.driver.rtos.mutexUnlock = mutexUnlock;
  Mod_Modem.driver.rtos.eventSet = eventSet;
  Mod_Modem.driver.rtos.eventWait = eventWait;
  Mod_Modem.driver.rtos.eventClear = eventClear;

  if (QTEL_Init(&Mod_Modem.driver) != QTEL_OK) return false;

  // setup APN
  QTEL_NET_SetupAPN(&Mod_Modem.driver.net, CONF_APN, CONF_APN_USER, CONF_APN_PASS);

  // setup NTP
#if QTEL_EN_FEATURE_NTP
  Mod_Modem.driver.ntp.onSynced = onNTPSynced;
  Mod_Modem.driver.ntp.config.resyncInterval = 24 * 3600 * 1000;
  Mod_Modem.driver.ntp.config.retryInterval = 10 * 1000;
  QTEL_NTP_SetupServer(&Mod_Modem.driver.ntp, CONF_NTP_SERVER, CONF_NTP_REGION);
#endif

  return true;
}


void MDM_Run(OS_TYPEDEF_THREAD_ARG arg)
{
  MDM_Init();

  OS_WaitReady();

  reset:
  if (!MDM_Start()) {
    OS_Delay(1000);
    goto reset;
  }
  isEventReset = false;

  OS_EventSet(MDM_Event, MDM_EVT_INIT_SUCCESS);

  // test to wait sim ready
  QTEL_Thread_Run(&Mod_Modem.driver);
  goto reset;
}


void MDM_Simcom_Run(OS_TYPEDEF_THREAD_ARG srg)
{
  uint32_t notif;

start:
  if (!OS_IsError(OS_EventWait(MDM_Event, MDM_EVT_INIT_SUCCESS, &notif, OS_WaitAny, OS_WaitForever))) {
    if (OS_IsFlag(notif, MDM_EVT_INIT_SUCCESS)) {
      goto run;
    }
  }
  goto start;

run:
  for (;;) {
    AT_Process(&Mod_Modem.driver.atCmd);
  }
}

bool MDM_Start(void)
{
  HAL_GPIO_WritePin(GPIOB, Modem_Pwr_Pin, GPIO_PIN_RESET);
  OS_Delay(100);
  HAL_GPIO_WritePin(GPIOB, Modem_Pwr_Pin, GPIO_PIN_SET);
  OS_Delay(100);

  return true;
}

void MDM_SendUSSD(const char *ussd)
{
//  SIM_SendUSSD(&(Mod_Modem.driver), ussd);
}

void MDM_Reset(void)
{
  isEventReset = true;
  while (isEventReset) OS_Delay(10);
}

bool MDM_WaitReady(uint32_t timeout)
{
  uint32_t tick = OS_GetTick();

  while (true) {
    if (timeout == 0 || OS_IsTimeout(tick, timeout)) {
      return false;
    }
    OS_Delay(10);
  }

  return true;
}

bool MDM_WaitOnline(uint32_t timeout)
{
  uint32_t tick = OS_GetTick();

  while (Mod_Modem.driver.net.state != QTEL_NET_STATE_ONLINE) {
    if (timeout == 0 || OS_IsTimeout(tick, timeout)) {
      return false;
    }
    OS_Delay(10);
  }

  return true;
}

#if SIM_EN_FEATURE_GPS
bool MDM_IsGPSWeakSignal(void)
{
  return (Mod_Modem.driver.gps.lwgps.dop_h <= 0.0f || Mod_Modem.driver.gps.lwgps.dop_h > 2.0f);
}
#endif


static int serialRead(uint8_t *dst, uint16_t sz)
{
  if (!STRM_IsReadable(&mdm_hdma_streamer)) {
    OS_Delay(10);
    return 0;
  }
  return STRM_Read(&mdm_hdma_streamer, dst, sz, 5000);
}


static int serialReadline(uint8_t *dst, uint16_t sz)
{
  if (!STRM_IsReadable(&mdm_hdma_streamer)) {
    OS_Delay(10);
    return 0;
  }
  return STRM_Readline(&mdm_hdma_streamer, dst, sz, 5000);
}


static int serialReadinto(void *buffer, uint16_t sz)
{
  return STRM_ReadIntoBuffer(&mdm_hdma_streamer, (Buffer_t *) buffer, sz, 5000);
}

static int serialWrite(uint8_t *src, uint16_t sz)
{
  return STRM_Write(&mdm_hdma_streamer, src, sz, STRM_BREAK_NONE);
}


static AT_Status_t mutexLock(uint32_t timeout)
{
  OS_MutexAcquire(MDM_Mutex, OS_WaitForever);
  return AT_OK;
}


static AT_Status_t mutexUnlock(void)
{
  OS_MutexRelease(MDM_Mutex);
  return AT_OK;
}


static AT_Status_t eventSet(uint32_t events)
{
  OS_EventSet(SIMCOM_Event, events);
  return AT_OK;
}


static AT_Status_t eventWait(uint32_t events, uint32_t *onEvents, uint32_t timeout)
{
  if (OS_IsError(OS_EventWait(SIMCOM_Event, events, onEvents, OS_WaitAny, timeout))) return AT_ERROR;
  return AT_OK;
}


static AT_Status_t eventClear(uint32_t events)
{
  OS_EventClear(SIMCOM_Event, events);
  return AT_OK;
}


#if RTC_ENABLE
static void onNTPSynced(QTEL_Datetime_t simDT)
{
  Datetime_t result;
  memcpy(&result, &simDT, sizeof(Datetime_t));
  // RTC_Write(&result);
}
#endif
