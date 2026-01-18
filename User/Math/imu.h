#ifndef __IMU_H
#define __IMU_H

#include "ALL_DATA.h"

//extern float GetNormAccz(void);
extern void GetAngle(const MPU_t* pMpu, Ange_t* pAngE, float dt);
extern void imu_rest(void);
#endif
