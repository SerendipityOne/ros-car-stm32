#ifndef __ALL_USER_DATA_H
#define __ALL_USER_DATA_H
// *****************************************************************************
#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

/* exact-width signed integer types */
#ifndef INT8_MIN
typedef signed char int8_t;
#endif
#ifndef INT16_MIN
typedef signed short int int16_t;
#endif
#ifndef INT32_MIN
typedef signed int int32_t;
#endif
#ifndef INT64_MIN
typedef signed long long int64_t;
#endif

/* exact-width unsigned integer types */
#ifndef UINT8_MAX
typedef unsigned char uint8_t;
#endif
#ifndef UINT16_MAX
typedef unsigned short int uint16_t;
#endif
#ifndef UINT32_MAX
typedef unsigned int uint32_t;
#endif
#ifndef UINT64_MAX
typedef unsigned long long uint64_t;
#endif

// *****************************************************************************
#define u8   uint8_t
#define u16  uint16_t
#define u32  uint32_t

#define RUN  1
#define STOP 0

#define __IO volatile
// *****************************************************************************
//#define NULL 0
extern volatile uint32_t uwTick;  //    系统滴答计数器
// *****************************************************************************
// typedef enum {
//  FALSE = 0,
//  TRUE = !FALSE
// } bool;

typedef struct {
    int16_t accX;   // 加速度计X轴
    int16_t accY;   // 加速度计Y轴
    int16_t accZ;   // 加速度计Z轴
    int16_t gyroX;  // 陀螺仪X轴
    int16_t gyroY;  // 陀螺仪Y轴
    int16_t gyroZ;  // 陀螺仪Z轴
} MPU_t;

typedef struct {
    float roll;   // 横滚角
    float pitch;  // 俯仰角
    float yaw;    // 偏航角
} Ange_t;

typedef volatile struct {
    float desired;         // 期望值
    float offset;          // 偏移量
    float prevError;       // 上次误差
    float integ;           // 积分项
    float kp;              // 比例系数
    float ki;              // 积分系数
    float kd;              // 微分系数
    float IntegLimitHigh;  // 积分上限
    float IntegLimitLow;   // 积分下限
    float measured;        // 测量值
    float out;             // 输出值
    float OutLimitHigh;    // 输出上限
    float OutLimitLow;     // 输出下限
} PID_t;

// *****************************************************************************
extern MPU_t g_mpu6050_data;
extern Ange_t g_angle;

extern PID_t pidRateX;
extern PID_t pidRateY;
extern PID_t pidRateZ;

extern PID_t pidPitch;
extern PID_t pidRoll;
extern PID_t pidYaw;

extern int16_t motor_pwm_value[4];

#endif