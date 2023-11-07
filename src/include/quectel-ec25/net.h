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


#define QTEL_NET_STATUS_CTX_CONFIGURED   0x01
#define QTEL_NET_STATUS_CTX_ACTIVED      0x02
#define QTEL_NET_STATUS_GPRS_REGISTERED  0x10
#define QTEL_NET_STATUS_GPRS_ROAMING     0x20
#define QTEL_NET_STATUS_NTP_WAS_SET      0x40
#define QTEL_NET_STATUS_NTP_WAS_SYNCED   0x80

typedef struct {
  void      *qtel;         // QTEL_HandlerTypeDef
  uint16_t  isCtxConfigured;

  struct {
    char *APN;
    char *user;
    char *pass;
  } APN;

  void (*onOpening)(void);
  void (*onOpened)(void);
  void (*onOpenError)(void);
  void (*onClosed)(void);
} QTEL_NET_HandlerTypeDef;


QTEL_Status_t QTEL_NET_Init(QTEL_NET_HandlerTypeDef*, void *hsim);
void          QTEL_NET_SetupAPN(QTEL_NET_HandlerTypeDef*, char *APN, char *user, char *pass);

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
