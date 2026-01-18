#ifndef __FLASH_H
#define __FLASH_H

#include "ALL_DATA.h"
#include "main.h"

void MpuOffset_Read(int16_t out[6]);
HAL_StatusTypeDef MpuOffset_Write(const int16_t in[6]);

#endif  // !__FLASH_H
