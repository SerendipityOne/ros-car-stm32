#include "ALL_DATA.h"
#include "ALL_DEFINE.h"

MPU_t MPU6050;
Ange_t Angle;

pid_t pidRateX;
pid_t pidRateY;
pid_t pidRateZ;
pid_t pidPitch;
pid_t pidRoll;
pid_t pidYaw;

int16_t motor_pwm_value[4];

void All_Init(void) {
	LED_Init();
    // MPU6050_Init();
    // Motor_Init();
}

void NVIC_Init(void) {
}

void PID_Param_Init(void) {  //PID参数初始化

    // pidRateX.kp = 3.0f;
    // pidRateY.kp = 3.0f;
    // pidRateZ.kp = 6.0f;

    // pidRateX.ki = 0.1f;
    // pidRateY.ki = 0.1f;
    // pidRateZ.ki = 0.1f;

    // pidRateX.kd = 0.5f;
    // pidRateY.kd = 0.5f;
    // pidRateZ.kd = 0.5f;

    // pidPitch.kp = 10.0f;
    // pidRoll.kp = 10.0f;
    // pidYaw.kp = 8.0f;
}
