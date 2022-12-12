#ifndef SIMCOM_7600E_TYPES_H
#define SIMCOM_7600E_TYPES_H

#include <stdint.h>

typedef enum {
  SIM_OK,
  SIM_ERROR,
  SIM_TIMEOUT
} SIM_Status_t;

typedef struct {
  uint8_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  int8_t  timezone;
} SIM_Datetime_t;

#endif /* SIMCOM_7600E_TYPES_H*/
