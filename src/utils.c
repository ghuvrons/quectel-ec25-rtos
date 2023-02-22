/*
 * core.c
 *
 *  Created on: Nov 8, 2022
 *      Author: janoko
 */

#include <quectel-ec25.h>
#include <quectel-ec25/utils.h>

#define num_days_in_month(month, year) (mounth_days[(month)-1] + ((((year)%4) == 0 && (month) == 2)? 1: 0))

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
  int addMinutes  = (addSeconds - (addSeconds%60))/60;
  int addHours    = (addMinutes - (addMinutes%60))/60;
  int addDays     = (addHours   - (addHours  %24))/24;

  addSeconds %= 60;
  addMinutes %= 60;
  addHours   %= 24;

  // add seconds
  if (dt->second < -addSeconds) {
    dt->second += 24;
    addMinutes--;
  }
  dt->second += addSeconds;
  if (dt->second >= 60) {
    dt->second -= 60;
    addMinutes++;
  }

  // add minutes

  if (dt->minute < -addMinutes) {
    dt->minute += 60;
    addHours--;
  }
  dt->minute += addMinutes;
  if (dt->minute >= 60) {
    dt->minute -= 60;
    addHours++;
  }

  // add hours
  if (dt->hour < -addHours) {
    dt->hour += 24;
    addDays--;
  }
  dt->hour += addHours;
  if (dt->hour >= 24) {
    dt->hour -= 24;
    addDays++;
  }

  QTEL_Datetime_AddDays(dt, addDays);
}


void QTEL_Datetime_AddDays(QTEL_Datetime_t *dt, int days)
{
  int tmpAdd;
  int curr_month_days;

  if (QTEL_Datetime_IsValid(dt) != QTEL_OK) return;

  while (days != 0) {
    curr_month_days = (int) num_days_in_month(dt->month, dt->year);

    if (days > 0) {
      if (days > (curr_month_days-dt->day)) {
        tmpAdd = curr_month_days - dt->day + 1;
      }
      else tmpAdd = days;
      if (tmpAdd > days) break;
    }
    else {
      if (-days >= dt->day) {
        tmpAdd = -dt->day;
      }
      else tmpAdd = days;
      if (tmpAdd < days) break;
    }

    dt->day = (uint8_t) (((int)dt->day) + tmpAdd);
    if (dt->day > curr_month_days) {
      dt->day -= curr_month_days;
      dt->month++;
      if (dt->month > 12) {
        dt->month = 1;
        dt->year += 1;
      }
    } else if (dt->day == 0) {
      dt->month--;
      if (dt->month == 0) {
        dt->year -= 1;
        dt->month = 12;
      }
      dt->day = num_days_in_month(dt->month, dt->year);
    }
    days -= tmpAdd;
  }
}

// start from jan 2000
int QTEL_Datetime_ToSeconds(const QTEL_Datetime_t *dt)
{
  const int sInYear   = 31536000;
  const int sInDay    = 86400;
  const int sInHour   = 1440;
  const int sInMinute = 60;
  int numSeconds      = 0;

  // seconds in years before
  numSeconds += dt->year * sInYear;

  // seconds in leap year
  numSeconds += ((dt->year-(dt->year%4)/4) + 1) * sInDay;

  // seconds in months before
  for (uint8_t i = 1; i < dt->month; i++) {
    numSeconds += ((int)num_days_in_month(i, dt->year)) * sInDay;
  }

  // seconds in days before
  if (dt->day > 0)
    numSeconds += (dt->day - 1) * sInDay;

  numSeconds += dt->hour * sInHour;
  numSeconds += dt->minute * sInMinute;
  numSeconds += dt->second;

  return numSeconds;
}


int QTEL_Datetime_Diff(const QTEL_Datetime_t *dt1, const QTEL_Datetime_t *dt2)
{
  return QTEL_Datetime_ToSeconds(dt1) - QTEL_Datetime_ToSeconds(dt2);
}

