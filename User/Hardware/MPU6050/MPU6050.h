#ifndef __MPU6050_H
#define __MPU6050_H

#include "MPU6050_Regs.h"
#include "main.h"

HAL_StatusTypeDef MPU6050_Init(void);
HAL_StatusTypeDef MPU_GetData(void);
void MPU_SetOffset(void);

#endif  // !__MPU6050_H
