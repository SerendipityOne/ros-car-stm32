#include "Motor.h"
#include "my_math.h"

extern TIM_HandleTypeDef htim8;

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

/**
 * @brief 初始化电机控制模块
 * @note 启动TIM8的四个通道用于PWM输出，并将所有电机设置为滑行停止状态
 */
void Motor_Init(void) {
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_4);

    // 上电默认全部滑行停止（coast）
    SetDir(TB1_AIN1_GPIO_Port, TB1_AIN1_Pin, TB1_AIN2_GPIO_Port, TB1_AIN2_Pin, GPIO_PIN_RESET, GPIO_PIN_RESET);
    SetDir(TB1_BIN1_GPIO_Port, TB1_BIN1_Pin, TB1_BIN2_GPIO_Port, TB1_BIN2_Pin, GPIO_PIN_RESET, GPIO_PIN_RESET);
    SetDir(TB2_AIN1_GPIO_Port, TB2_AIN1_Pin, TB2_AIN2_GPIO_Port, TB2_AIN2_Pin, GPIO_PIN_RESET, GPIO_PIN_RESET);
    SetDir(TB2_BIN1_GPIO_Port, TB2_BIN1_Pin, TB2_BIN2_GPIO_Port, TB2_BIN2_Pin, GPIO_PIN_RESET, GPIO_PIN_RESET);

    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_3, 0);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_4, 0);
}

/**
 * @brief 设置四个电机的PWM值
 * @param motor_pwm_value 包含四个电机PWM值的数组，正值表示正转，负值表示反转，零表示停止
 * @note 数组索引对应：[0]=M1, [1]=M2, [2]=M3, [3]=M4
 */
void Motor_SetPWM(const int16_t motor_pwm_value[4]) {
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim8);

    for (int i = 0; i < 4; i++) {
        int16_t v = motor_pwm_value[i];

        GPIO_PinState in1 = GPIO_PIN_RESET;
        GPIO_PinState in2 = GPIO_PIN_RESET;

        if (v > 0) {
            in1 = GPIO_PIN_SET;
            in2 = GPIO_PIN_RESET;
        }
        if (v < 0) {
            in1 = GPIO_PIN_RESET;
            in2 = GPIO_PIN_SET;
        }

        uint32_t ccr = 0;
        if (v != 0) {
            ccr = min((uint32_t)ABS(v), arr);
        }

        switch (i) {
            case 0:  // M1 -> TIM8_CH1
                SetDir(TB1_AIN1_GPIO_Port, TB1_AIN1_Pin, TB1_AIN2_GPIO_Port, TB1_AIN2_Pin, in1, in2);
                __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, ccr);
                break;

            case 1:  // M2 -> TIM8_CH2
                SetDir(TB1_BIN1_GPIO_Port, TB1_BIN1_Pin, TB1_BIN2_GPIO_Port, TB1_BIN2_Pin, in1, in2);
                __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, ccr);
                break;

            case 2:  // M3 -> TIM8_CH3
                SetDir(TB2_AIN1_GPIO_Port, TB2_AIN1_Pin, TB2_AIN2_GPIO_Port, TB2_AIN2_Pin, in1, in2);
                __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_3, ccr);
                break;

            default:  // M4 -> TIM8_CH4
                SetDir(TB2_BIN1_GPIO_Port, TB2_BIN1_Pin, TB2_BIN2_GPIO_Port, TB2_BIN2_Pin, in1, in2);
                __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_4, ccr);
                break;
        }
    }
}
