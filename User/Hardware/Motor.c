#include "Motor.h"

// 注意：关于PWM频率
// 基于您的TIM8配置：PSC=0, ARR=8400-1 且 TIM8时钟~= 168 MHz，
// PWM频率 ~= 168e6 / (PSC+1) / (ARR+1) = 168e6 / 8400 = 20 kHz.
// (如果APB2预分频器 != 1, TIM8时钟可能是2*PCLK2; 使用CubeMX时钟树作为标准.)

extern TIM_HandleTypeDef htim8;

// ===================== 电机输出配置 =====================
// 这些变量特意保留在Motor.c中，以便项目的其他部分可以将
// 此模块视为纯"执行器输出"层.

static uint8_t g_motor_enabled = 1;
static Motor_StopMode_t g_stop_mode = MOTOR_STOP_COAST;
// bit0=M1, bit1=M2, bit2=M3, bit3=M4 (1表示反转)
static uint8_t g_dir_invert_mask = 0;

static inline int16_t clamp_i16(int16_t v, int16_t lo, int16_t hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline int16_t abs_i16(int16_t v) {
    return (v >= 0) ? v : (int16_t)(-v);
}

static inline uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

/**
 * @brief 设置电机方向引脚状态
 * @param port1 第一个GPIO端口指针
 * @param pin1 第一个GPIO引脚号
 * @param port2 第二个GPIO端口指针
 * @param pin2 第二个GPIO引脚号
 * @param s1 第一个引脚的状态
 * @param s2 第二个引脚的状态
 */
static inline void SetDir(GPIO_TypeDef* port1, uint16_t pin1, GPIO_TypeDef* port2, uint16_t pin2, GPIO_PinState s1, GPIO_PinState s2) {
    HAL_GPIO_WritePin(port1, pin1, s1);
    HAL_GPIO_WritePin(port2, pin2, s2);
}

// 根据带符号的命令（经过反转后）应用单个电机的方向引脚.
static inline void Motor_ApplyDir(uint8_t idx, int16_t v_signed) {
    GPIO_PinState in1 = GPIO_PIN_RESET;
    GPIO_PinState in2 = GPIO_PIN_RESET;

    if (v_signed > 0) {
        in1 = GPIO_PIN_SET;
        in2 = GPIO_PIN_RESET;
    } else if (v_signed < 0) {
        in1 = GPIO_PIN_RESET;
        in2 = GPIO_PIN_SET;
    } else {
        // v == 0
        if (g_stop_mode == MOTOR_STOP_BRAKE) {
            in1 = GPIO_PIN_SET;
            in2 = GPIO_PIN_SET;
        } else {
            in1 = GPIO_PIN_RESET;
            in2 = GPIO_PIN_RESET;
        }
    }

    switch (idx) {
        case 0: // M1
            SetDir(TB1_AIN1_GPIO_Port, TB1_AIN1_Pin, TB1_AIN2_GPIO_Port, TB1_AIN2_Pin, in1, in2);
            break;
        case 1: // M2
            SetDir(TB1_BIN1_GPIO_Port, TB1_BIN1_Pin, TB1_BIN2_GPIO_Port, TB1_BIN2_Pin, in1, in2);
            break;
        case 2: // M3
            SetDir(TB2_AIN1_GPIO_Port, TB2_AIN1_Pin, TB2_AIN2_GPIO_Port, TB2_AIN2_Pin, in1, in2);
            break;
        default: // M4
            SetDir(TB2_BIN1_GPIO_Port, TB2_BIN1_Pin, TB2_BIN2_GPIO_Port, TB2_BIN2_Pin, in1, in2);
            break;
    }
}

static inline void Motor_SetCompare(uint8_t idx, uint32_t ccr) {
    switch (idx) {
        case 0:
            __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, ccr);
            break;
        case 1:
            __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, ccr);
            break;
        case 2:
            __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_3, ccr);
            break;
        default:
            __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_4, ccr);
            break;
    }
}

/**
 * @brief 初始化电机控制模块
 * @note 启动TIM8的四个通道用于PWM输出，并将所有电机设置为滑行停止状态
 */
void Motor_Init(void) {
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_4);

    g_motor_enabled = 1;
    g_stop_mode = MOTOR_STOP_COAST;
    g_dir_invert_mask = 0;

    Motor_StopAll();
}

void Motor_Enable(uint8_t enable) {
    g_motor_enabled = enable ? 1 : 0;
    if (!g_motor_enabled) {
        Motor_StopAll();
    }
}

void Motor_StopAll(void) {
    // 强制所有电机为0
    for (uint8_t i = 0; i < 4; i++) {
        Motor_ApplyDir(i, 0);
        Motor_SetCompare(i, 0);
    }
}

void Motor_SetStopMode(Motor_StopMode_t mode) {
    g_stop_mode = mode;
}

void Motor_SetDirInvertMask(uint8_t mask) {
    g_dir_invert_mask = mask;
}

// 归一化输入：[-1000,1000]
void Motor_SetPWM_Norm(const int16_t pwm_norm_value[4]) {
    if (!g_motor_enabled) {
        Motor_StopAll();
        return;
    }

    // ARR是在CubeMX中配置的（您使用ARR=8400-1）。我们在运行时读取它，
    // 以便即使您以后更改PWM频率，Motor_SetPWM_Norm也能保持正确.
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim8);
    uint32_t top = arr + 1u;

    for (uint8_t i = 0; i < 4; i++) {
        int16_t v = clamp_i16(pwm_norm_value[i], -1000, 1000);
        // 可选的方向反转
        if (g_dir_invert_mask & (1u << i)) {
            v = (int16_t)(-v);
        }

        Motor_ApplyDir(i, v);

        uint32_t ccr = 0;
        if (v != 0) {
            // 映射 [-1000,1000] -> [0,ARR]. 使用(ARR+1)可以获得稍好的
            // 缩放精度. 限制为ARR以避免CRR > ARR.
            ccr = ((uint32_t)abs_i16(v) * top) / 1000u;
            if (ccr > arr) ccr = arr;
        }
        Motor_SetCompare(i, ccr);
    }
}

/**
 * @brief 设置四个电机的PWM值
 * @param motor_pwm_value 包含四个电机PWM值的数组，正值表示正转，负值表示反转，零表示停止
 * @note 数组索引对应：[0]=M1, [1]=M2, [2]=M3, [3]=M4
 */
void Motor_SetPWM(const int16_t motor_pwm_value[4]) {
    if (!g_motor_enabled) {
        Motor_StopAll();
        return;
    }

    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim8);

    for (uint8_t i = 0; i < 4; i++) {
        int16_t v = motor_pwm_value[i];
        // 可选的方向反转
        if (g_dir_invert_mask & (1u << i)) {
            v = (int16_t)(-v);
        }

        // v被解释为[-ARR, ARR]范围内的有符号CCR计数值
        uint32_t ccr = 0;
        if (v != 0) {
            ccr = min_u32((uint32_t)abs_i16(v), arr);
        }

        Motor_ApplyDir(i, v);
        Motor_SetCompare(i, ccr);
    }
}