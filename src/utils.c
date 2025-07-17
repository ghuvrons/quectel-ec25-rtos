/*
 * core.c
 *
 *  Created on: Nov 8, 2022
 *      Author: janoko
 */

#include <quectel-ec25.h>
#include <quectel-ec25/utils.h>
#include <time.h>

#define num_days_in_month(month, year) (mounth_days[(month)-1] + ((((year)%4) == 0 && (month) == 2)? 1: 0))
#define IS_DATETIME_VALID(dt) ((dt)->month < 1 || (dt)->month > 12 || \
                               (dt)->day < 1 || (dt)->day > 31)

const uint8_t mounth_days[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};


QTEL_Status_t QTEL_Datetime_IsValid(const QTEL_Datetime_t *dt)
{
  if (dt->month == 0 || dt->month > 12) return QTEL_ERROR;
  if (dt->day == 0 ||
      dt->day > num_days_in_month(dt->month, dt->year)) return QTEL_ERROR;
  if (dt->hour >= 24) return QTEL_ERROR;
  if (dt->minute >= 60) return QTEL_ERROR;
  if (dt->second >= 60) return QTEL_ERROR;
  return QTEL_OK;
}


void QTEL_Datetime_SetToUTC(QTEL_Datetime_t *dt)
{
  int addSeconds = dt->timezone * -900;
  QTEL_Datetime_AddSeconds(dt, addSeconds);
  dt->timezone = 0;
}


void QTEL_Datetime_AddSeconds(QTEL_Datetime_t *dt, int addSeconds)
{
  struct tm _tm;
  time_t ts;

  if (IS_DATETIME_VALID(dt)) return;

  _tm.tm_year = dt->year + 100; // Contoh: tahun 2023 akan menjadi 123
  _tm.tm_mon  = dt->month - 1;   // QTEL_Datetime_t.month dimulai dari 1
  _tm.tm_mday = dt->day;
  _tm.tm_hour = dt->hour;
  _tm.tm_min  = dt->minute;
  _tm.tm_sec  = dt->second;

  ts = mktime(&_tm);
  ts += addSeconds;
  localtime_r(&ts, &_tm);

  dt->year    = _tm.tm_year - 100;
  dt->month   = _tm.tm_mon + 1;
  dt->day     = _tm.tm_mday;
  dt->hour    = _tm.tm_hour;
  dt->minute  = _tm.tm_min;
  dt->second  = _tm.tm_sec;
}

// start from jan 2000
uint32_t QTEL_Datetime_ToSeconds(const QTEL_Datetime_t *dt)
{
  struct tm _tm;
  time_t ts;

  if (IS_DATETIME_VALID(dt)) return 0;

  _tm.tm_year = dt->year + 100; // Contoh: tahun 2023 akan menjadi 123
  _tm.tm_mon  = dt->month - 1;   // QTEL_Datetime_t.month dimulai dari 1
  _tm.tm_mday = dt->day;
  _tm.tm_hour = dt->hour;
  _tm.tm_min  = dt->minute;
  _tm.tm_sec  = dt->second;

  ts = mktime(&_tm);
  return (uint32_t) ts;
}


int QTEL_Datetime_Diff(const QTEL_Datetime_t *dt1, const QTEL_Datetime_t *dt2)
{
  return QTEL_Datetime_ToSeconds(dt1) - QTEL_Datetime_ToSeconds(dt2);
}

