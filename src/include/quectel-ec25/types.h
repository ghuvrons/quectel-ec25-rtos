#ifndef QUECTEL_EC25_TYPES_H
#define QUECTEL_EC25_TYPES_H

#include <stdint.h>

typedef enum {
  QTEL_OK,
  QTEL_ERROR,
  QTEL_TIMEOUT
} QTEL_Status_t;

typedef struct {
  uint8_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  int8_t  timezone;
} QTEL_Datetime_t;

#endif /* QUECTEL_EC25_TYPES_H*/
