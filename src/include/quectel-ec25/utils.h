/*

 * utils.h
 *
 *  Created on: Dec 3, 2021
 *      Author: janoko
 */

#ifndef QUECTEL_EC25_UTILS_H_
#define QUECTEL_EC25_UTILS_H_

#include <quectel-ec25.h>
#include <string.h>


#define QTEL_IsTimeout(qtelPtr, lastTick, timeout) (((qtelPtr)->getTick() - (lastTick)) > (timeout))


#define QTEL_BITS_IS_ALL(bits, bit) (((bits) & (bit)) == (bit))
#define QTEL_BITS_IS_ANY(bits, bit) ((bits) & (bit))
#define QTEL_BITS_IS(bits, bit)     QTEL_BITS_IS_ALL(bits, bit)
#define QTEL_BITS_SET(bits, bit)    {(bits) |= (bit);}
#define QTEL_BITS_UNSET(bits, bit)  {(bits) &= ~(bit);}

#define QTEL_IS_STATUS(qtelPtr, stat)     QTEL_BITS_IS_ALL((qtelPtr)->status, stat)
#define QTEL_SET_STATUS(qtelPtr, stat)    QTEL_BITS_SET((qtelPtr)->status, stat)
#define QTEL_UNSET_STATUS(qtelPtr, stat)  QTEL_BITS_UNSET((qtelPtr)->status, stat)

#if SIM_EN_FEATURE_MQTT
#define SIM_MQTT_IS_STATUS(qtelPtr, stat)     QTEL_BITS_IS_ALL((qtelPtr)->mqtt.status, stat)
#define SIM_MQTT_SET_STATUS(qtelPtr, stat)    QTEL_BITS_SET((qtelPtr)->mqtt.status, stat)
#define SIM_MQTT_UNSET_STATUS(qtelPtr, stat)  QTEL_BITS_UNSET((qtelPtr)->mqtt.status, stat)
#endif

#if QTEL_EN_FEATURE_GPS
#define QTEL_GPS_IS_STATUS(qtelPtr, stat)     QTEL_BITS_IS_ALL((qtelPtr)->gps.status, stat)
#define QTEL_GPS_SET_STATUS(qtelPtr, stat)    QTEL_BITS_SET((qtelPtr)->gps.status, stat)
#define QTEL_GPS_UNSET_STATUS(qtelPtr, stat)  QTEL_BITS_UNSET((qtelPtr)->gps.status, stat)
#endif


QTEL_Status_t QTEL_Datetime_IsValid(QTEL_Datetime_t*);
void QTEL_Datetime_AddDays(QTEL_Datetime_t*, int days);
int8_t QTEL_Datetime_CompareDate(QTEL_Datetime_t*, QTEL_Datetime_t*);

#endif /* QUECTEL_EC25_UTILS_H_ */
