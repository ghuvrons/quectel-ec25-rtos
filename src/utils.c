/*
 * core.c
 *
 *  Created on: Nov 8, 2022
 *      Author: janoko
 */

#include <quectel-ec25.h>
#include <quectel-ec25/utils.h>

const uint8_t mounth_days[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};


QTEL_Status_t QTEL_Datetime_IsValid(QTEL_Datetime_t *dt)
{
  if (dt->month == 0 || dt->month > 12) return QTEL_ERROR;
  if (dt->day == 0 || dt->day > mounth_days[dt->month-1]) return QTEL_ERROR;
  if (dt->hour >= 24) return QTEL_ERROR;
  if (dt->minute >= 60) return QTEL_ERROR;
  if (dt->second >= 60) return QTEL_ERROR;
  return QTEL_OK;
}


void QTEL_Datetime_AddDays(QTEL_Datetime_t *dt, int days)
{
  int tmpAdd;
  int curr_month_days;

  if (QTEL_Datetime_IsValid(dt) != QTEL_OK) return;

  while (days != 0) {
    curr_month_days = (int) mounth_days[dt->month-1];

    if (days > (curr_month_days-dt->day)) {
      tmpAdd = curr_month_days - dt->day + 1;
    }
    else tmpAdd = days;

    dt->day += tmpAdd;
    if (dt->day > curr_month_days) {
      dt->day -= curr_month_days;
      dt->month++;
      if (dt->month > 12) {
        dt->month = 1;
        dt->year += 1;
      }
    }
    days -= tmpAdd;
  }
}


int8_t QTEL_Datetime_CompareDate(QTEL_Datetime_t *dt1, QTEL_Datetime_t *dt2)
{
  if (dt1->year > dt2->year) return 1;
  else if (dt1->year < dt2->year) return -1;
  if (dt1->month > dt2->month) return 1;
  else if (dt1->month < dt2->month) return -1;
  if (dt1->day > dt2->day) return 1;
  else if (dt1->day < dt2->day) return -1;
  return 0;
}

