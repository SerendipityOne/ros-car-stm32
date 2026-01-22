#include "ALL_DATA.h"
#include "ALL_DEFINE.h"

MPU_t g_mpu6050_data;
Ange_t Angle;

PID_t pidRateX;
PID_t pidRateY;
PID_t pidRateZ;
PID_t pidPitch;
PID_t pidRoll;
PID_t pidYaw;

int16_t motor_pwm_value[4];

void ALL_Init(void) {
    OLED_Init();
    Motor_Init();
    // MPU6050_Init(); // 在MPU6050_Task中初始化
}
