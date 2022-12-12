/*

 * core.h
 *
 *  Created on: Nov 8, 2022
 *      Author: janoko
 */

#ifndef QUECTEL_EC25_CORE_H
#define QUECTEL_EC25_CORE_H

#include <quectel-ec25.h>

QTEL_Status_t QTEL_Echo(QTEL_HandlerTypeDef*, uint8_t onoff);
QTEL_Status_t QTEL_CheckAT(QTEL_HandlerTypeDef*);
QTEL_Status_t QTEL_CheckSIMCard(QTEL_HandlerTypeDef*);
QTEL_Status_t QTEL_CheckNetwork(QTEL_HandlerTypeDef*);
QTEL_Status_t QTEL_ReqisterNetwork(QTEL_HandlerTypeDef*);
QTEL_Status_t QTEL_GetTime(QTEL_HandlerTypeDef*, QTEL_Datetime_t*);
QTEL_Status_t QTEL_CheckSugnal(QTEL_HandlerTypeDef*);

#endif /* QUECTEL_EC25_CORE_H */
