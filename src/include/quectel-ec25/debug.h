/*

 * debug.h
 *
 *  Created on: Feb 2, 2022
 *      Author: janoko
 */


#ifndef QUECTEL_EC25_DEBUG_H_
#define QUECTEL_EC25_DEBUG_H_

#include <quectel-ec25/conf.h>

#if QTEL_DEBUG
#include <stdio.h>


#define QTEL_Debug(...) {QTEL_Printf("QUECTEL: ");QTEL_Println(__VA_ARGS__);}

void QTEL_Printf(const char *format, ...);
void QTEL_Println(const char *format, ...);

#else
#define QTEL_Debug(...) {}
#endif /* QTEL_Debug */
#endif /* QUECTEL_EC25_DEBUG_H_ */
