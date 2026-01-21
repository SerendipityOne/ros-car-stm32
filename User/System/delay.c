#include "delay.h"

/**
  * @brief  微秒级延时
  * @param  xus 延时时长，单位：微秒
  * @retval 无
  */
void delay_us(uint32_t xus) {
    // 根据系统时钟频率计算延时循环次数，确保微秒级精度
    // 使用SystemCoreClock获取当前HCLK频率
    uint32_t cycles_per_us = SystemCoreClock / 1000000UL;
    __IO uint32_t delay = xus * cycles_per_us / 8;
    do {
        __NOP();
    } while (delay--);
}

/**
  * @brief  毫秒级延时
  * @param  xms 延时时长，单位：毫秒
  * @retval 无
  */
void delay_ms(uint32_t xms) {
#ifdef FREERTOS_CONFIG_H
    // 在FreeRTOS环境下，使用RTOS提供的延时函数，避免阻塞调度器
    vTaskDelay(xms);

#else
    // 裸机环境下使用循环实现毫秒延时
    while (xms--) {
        delay_us(1000);  // 调用微秒延时函数实现1ms延时
    }
#endif
}

/**
  * @brief  秒级延时
  * @param  xs 延时时长，单位：秒
  * @retval 无
  */
void delay_s(uint32_t xs) {
#ifdef FREERTOS_CONFIG_H
    // 在FreeRTOS环境下，使用RTOS提供的延时函数
    vTaskDelay(xs * 1000);
#else
    // 裸机环境下使用循环实现秒级延时
    while (xs--) {
        delay_us(1000);  // 调用微秒延时函数实现1ms延时
    }
#endif
}