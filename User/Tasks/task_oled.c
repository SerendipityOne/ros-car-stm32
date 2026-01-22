#include "task_shared.h"
#include "OLED.h"

/* OLED任务配置 */
#define OLED_STACK_SIZE 512
#define OLED_PRIORITY   3

static StackType_t oled_stack[OLED_STACK_SIZE];
static StaticTask_t oled_tcb;

static void oled_entry(void* arg) {
    (void)arg;

    /* 不定时执行：仅在收到信号量后刷新一次显示 */
    for (;;) {
        if (g_oled_sem != NULL && xSemaphoreTake(g_oled_sem, portMAX_DELAY) == pdTRUE) {
            OLED_Printf(0, 0, OLED_8X16, "Hello World!");
            OLED_Update();
        }
    }
}

/**
 * @brief 创建OLED任务
 *
 * OLED任务在创建后会阻塞等待 g_oled_sem。
 * 其他任务/中断调用 xSemaphoreGive(g_oled_sem) 即可触发一次显示。
 */
void OLED_TaskCreate(void) {
    xTaskCreateStatic(oled_entry, "oled", OLED_STACK_SIZE, NULL, OLED_PRIORITY, oled_stack, &oled_tcb);
}
