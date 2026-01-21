#ifndef __DELAY_H
#define __DELAY_H

#include "main.h"

#ifdef FREERTOS_CONFIG_H
#include "cmsis_os.h"
#endif

void delay_us(uint32_t us);
void delay_ms(uint32_t ms);
void delay_s(uint32_t s);

#endif