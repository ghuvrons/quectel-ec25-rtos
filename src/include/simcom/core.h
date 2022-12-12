/*
 * core.h
 *
 *  Created on: Nov 8, 2022
 *      Author: janoko
 */

#ifndef SIMCOM_7600E_CORE_H
#define SIMCOM_7600E_CORE_H

#include "../simcom.h"

SIM_Status_t SIM_Echo(SIM_HandlerTypeDef*, uint8_t onoff);
SIM_Status_t SIM_CheckAT(SIM_HandlerTypeDef*);
SIM_Status_t SIM_CheckSIMCard(SIM_HandlerTypeDef*);
SIM_Status_t SIM_CheckNetwork(SIM_HandlerTypeDef*);
SIM_Status_t SIM_ReqisterNetwork(SIM_HandlerTypeDef*);
SIM_Status_t SIM_GetTime(SIM_HandlerTypeDef*, SIM_Datetime_t*);
SIM_Status_t SIM_CheckSugnal(SIM_HandlerTypeDef*);

#endif /* SIMCOM_7600E_CORE_H */
