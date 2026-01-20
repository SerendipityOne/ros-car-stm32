#include "ALL_DATA.h"
#include "ALL_DEFINE.h"

MPU_t MPU6050;
Ange_t Angle;

PID_t pidRateX;
PID_t pidRateY;
PID_t pidRateZ;
PID_t pidPitch;
PID_t pidRoll;
PID_t pidYaw;

int16_t motor_pwm_value[4];

void All_Init(void) {
}

void ALL_Task(void) {
    StartLEDTask();
	UsbProto_OS_Init();  // 启动USB协议收发任务
}
