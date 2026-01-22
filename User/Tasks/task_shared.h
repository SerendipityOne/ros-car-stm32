#ifndef TASK_SHARED_H
#define TASK_SHARED_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "main.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

/**
 * @brief 创建启动任务（唯一入口）
 *
 * 启动任务负责：
 * 1. 创建所有工作任务
 * 2. 完成后删除自己
 */
void START_TaskCreate(void);

/* 以下函数由start任务内部调用，外部不应直接调用 */
void OLED_TaskCreate(void);
void LED_TaskCreate(void);
void MPU6050_TaskCreate(void);

/**
 * @brief OLED触发来源（用于OLED任务区分是谁触发了刷新）
 *
 * 说明：
 * - FreeRTOS 的信号量本身不携带“是谁give的”信息。
 * - 因此这里用一个队列传递 src，同时仍保留信号量用于唤醒OLED任务（兼容旧代码）。
 */
typedef enum {
    OLED_SRC_UNKNOWN = 0,
    OLED_SRC_LED = 1,
    OLED_SRC_USB = 2,
    OLED_SRC_MPU = 3,
} oled_src_t;

/* ===================== 共享RTOS对象 ===================== */

/* OLED任务共享信号量：
 * - 其他任务通过 xSemaphoreGive(g_oled_sem) 触发OLED任务执行一次显示（兼容旧代码）
 * - 推荐使用 OLED_Trigger(src) 来触发，这样OLED任务可以知道触发来源
 */
extern SemaphoreHandle_t g_oled_sem;
extern SemaphoreHandle_t g_mpu_mutex;  // MPU6050数据互斥量：用于保护全局MPU6050结构体的读写

/* OLED事件队列：用于携带触发来源 src（一个触发对应队列里一个src） */
extern QueueHandle_t g_oled_evt_q;

/* ===================== OLED触发API（推荐使用） ===================== */

void OLED_Trigger(oled_src_t src);

void OLED_TriggerFromISR(oled_src_t src, BaseType_t* pxHigherPriorityTaskWoken);

#endif
