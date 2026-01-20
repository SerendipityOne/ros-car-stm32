#ifndef __MOTOR_H
#define __MOTOR_H

#include "main.h"

// Stop behavior when PWM==0
typedef enum {
    MOTOR_STOP_COAST = 0,  // IN1=0, IN2=0
    MOTOR_STOP_BRAKE = 1   // IN1=1, IN2=1
} Motor_StopMode_t;

void Motor_Init(void);

// Enable/disable motor outputs. When disabled, outputs are forced to 0.
void Motor_Enable(uint8_t enable);

// Force all motors to stop (PWM=0) using current stop mode.
void Motor_StopAll(void);

// Optional: configure stop behavior when PWM==0.
void Motor_SetStopMode(Motor_StopMode_t mode);

// Optional: invert motor direction without changing wiring.
// bit0=M1, bit1=M2, bit2=M3, bit3=M4 (1 means invert)
void Motor_SetDirInvertMask(uint8_t mask);

// Set PWM in normalized units: [-1000, 1000]. (Recommended for control loops)
void Motor_SetPWM_Norm(const int16_t pwm_norm_value[4]);

// Legacy API: set PWM directly in timer counts (CCR). Range: [-ARR, ARR].
// With your current TIM8 config (PSC=0, ARR=8400-1), ARR=8399.
void Motor_SetPWM(const int16_t motor_pwm_value[4]);

#endif  // !__MOTOR_H
