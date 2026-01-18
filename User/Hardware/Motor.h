#ifndef __MOTOR_H
#define __MOTOR_H

#include "main.h"

void Motor_Init(void);
void Motor_SetPWM(const int16_t motor_pwm_value[4]);

#endif  // !__MOTOR_H
