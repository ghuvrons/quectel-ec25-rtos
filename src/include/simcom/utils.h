/*
 * utils.h
 *
 *  Created on: Dec 3, 2021
 *      Author: janoko
 */

#ifndef SIMCOM_7600E_UTILS_H_
#define SIMCOM_7600E_UTILS_H_

#include "../simcom.h"
#include <string.h>


#define SIM_IsTimeout(hsim, lastTick, timeout) (((hsim)->getTick() - (lastTick)) > (timeout))


#define SIM_BITS_IS_ALL(bits, bit) (((bits) & (bit)) == (bit))
#define SIM_BITS_IS_ANY(bits, bit) ((bits) & (bit))
#define SIM_BITS_IS(bits, bit)     SIM_BITS_IS_ALL(bits, bit)
#define SIM_BITS_SET(bits, bit)    {(bits) |= (bit);}
#define SIM_BITS_UNSET(bits, bit)  {(bits) &= ~(bit);}

#define SIM_IS_STATUS(hsim, stat)     SIM_BITS_IS_ALL((hsim)->status, stat)
#define SIM_SET_STATUS(hsim, stat)    SIM_BITS_SET((hsim)->status, stat)
#define SIM_UNSET_STATUS(hsim, stat)  SIM_BITS_UNSET((hsim)->status, stat)

#if SIM_EN_FEATURE_MQTT
#define SIM_MQTT_IS_STATUS(hsim, stat)     SIM_BITS_IS_ALL((hsim)->mqtt.status, stat)
#define SIM_MQTT_SET_STATUS(hsim, stat)    SIM_BITS_SET((hsim)->mqtt.status, stat)
#define SIM_MQTT_UNSET_STATUS(hsim, stat)  SIM_BITS_UNSET((hsim)->mqtt.status, stat)
#endif

#if SIM_EN_FEATURE_GPS
#define SIM_GPS_IS_STATUS(hsim, stat)     SIM_BITS_IS_ALL((hsim)->gps.status, stat)
#define SIM_GPS_SET_STATUS(hsim, stat)    SIM_BITS_SET((hsim)->gps.status, stat)
#define SIM_GPS_UNSET_STATUS(hsim, stat)  SIM_BITS_UNSET((hsim)->gps.status, stat)
#endif

#endif /* SIMCOM_7600E_UTILS_H_ */
