#ifndef __MYTASK_H
#define __MYTASK_H

#include "main.h"
#include "task.h"

// 任务优先级定义
#define LED_TASK_PRIO (tskIDLE_PRIORITY + 2)

// 函数声明
void StartLEDTask(void);

#endif /* __MYTASK_H */
