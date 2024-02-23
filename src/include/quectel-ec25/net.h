/*

 * net.h
 *
 *  Created on: Nov 9, 2022
 *      Author: janoko
 */

#ifndef QUECTEL_EC25_NET_H_
#define QUECTEL_EC25_NET_H_

#include <quectel-ec25/conf.h>
#if QTEL_EN_FEATURE_NET

#include <quectel-ec25/types.h>
typedef enum {
  QTEL_NET_STATE_NON_ACTIVE,
  QTEL_NET_STATE_ACTIVATING_PENDING,
  QTEL_NET_STATE_ACTIVATING,
  QTEL_NET_STATE_ACTIVE,
} QTEL_NET_State_t;

typedef struct {
  void              *qtel;         // QTEL_HandlerTypeDef
  QTEL_NET_State_t  state;
  uint16_t          isCtxConfigured;

  struct {
    char *APN;
    char *user;
    char *pass;
  } APN;
} QTEL_NET_HandlerTypeDef;


QTEL_Status_t QTEL_NET_Init(QTEL_NET_HandlerTypeDef*, void *hsim);
void          QTEL_NET_SetupAPN(QTEL_NET_HandlerTypeDef*, char *APN, char *user, char *pass);

QTEL_Status_t QTEL_NET_OnReboot(QTEL_NET_HandlerTypeDef*);
QTEL_Status_t QTEL_NET_Activate(QTEL_NET_HandlerTypeDef*, uint8_t isActive);

void QTEL_NET_SetState(QTEL_NET_HandlerTypeDef*, uint8_t newState);
void QTEL_NET_OnNewState(QTEL_NET_HandlerTypeDef*);

// Context

QTEL_Status_t QTEL_NET_ConfigurePDP(QTEL_NET_HandlerTypeDef*, uint8_t contextId);
QTEL_Status_t QTEL_NET_IsPDPActive(QTEL_NET_HandlerTypeDef*, uint8_t contextId, uint8_t *isActive);
QTEL_Status_t QTEL_NET_ActivatePDP(QTEL_NET_HandlerTypeDef*, uint8_t contextId);
QTEL_Status_t QTEL_NET_DeactivatePDP(QTEL_NET_HandlerTypeDef*, uint8_t contextId);

// Data Counter

QTEL_Status_t QTEL_NET_DataCounterReset(QTEL_NET_HandlerTypeDef*);
QTEL_Status_t QTEL_NET_GetDataCounter(QTEL_NET_HandlerTypeDef*, uint32_t *sent, uint32_t *recv);


#endif /* QTEL_EN_FEATURE_NET */
#endif /* QUECTEL_EC25_NET_H_ */
