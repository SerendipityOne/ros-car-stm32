#include "LED.h"
#include "task_shared.h"

// 任务优先级定义
#define LED_TASK_PRIO (tskIDLE_PRIORITY + 2)

/**
 * @brief LED闪烁任务函数
 * @param pvParameters 任务参数
 */
static void LED_Task(void* pvParameters) {
    // 初始化LED
    LED_Init();

    // 无限循环
    while (1) {
        LED1_Toggle;      // 翻转LED状态
        vTaskDelay(500);  // 延时500ms (实际延时500个tick，约0.5秒)
    }
}

/**
 * @brief 启动LED任务
 */
void LED_TaskCreate(void) {
    // 创建LED任务
    xTaskCreate(
        LED_Task,                      // 任务函数
        "LED Task",                    // 任务名称
        configMINIMAL_STACK_SIZE * 2,  // 任务堆栈大小
        NULL,                          // 任务参数
        LED_TASK_PRIO,                 // 任务优先级
        NULL                           // 任务句柄
    );
}