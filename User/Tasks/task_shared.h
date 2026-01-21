#ifndef TASK_SHARED_H
#define TASK_SHARED_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

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

/* ===================== 共享状态变量 ===================== */

/* ===================== LED任务接口 ===================== */

#endif
