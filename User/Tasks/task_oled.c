#include "task_shared.h"
#include "OLED.h"

/* OLED任务配置 */
#define OLED_STACK_SIZE 512
#define OLED_PRIORITY   3

static StackType_t oled_stack[OLED_STACK_SIZE];
static StaticTask_t oled_tcb;

static void oled_entry(void* arg) {
    (void)arg;

    for (;;) {
        /* 等待一次触发（计数信号量：每次give对应一次take） */
        if (g_oled_sem != NULL && xSemaphoreTake(g_oled_sem, portMAX_DELAY) == pdTRUE) {

            /* 取出触发来源：推荐触发方使用 OLED_Trigger(src)；
             * 若仍有人直接give信号量而不发队列，这里可能取不到src。 */
            oled_src_t src = OLED_SRC_UNKNOWN;
            if (g_oled_evt_q != NULL) {
                (void)xQueueReceive(g_oled_evt_q, &src, 0);
            }

            /* 根据来源显示不同内容（最小示例） */
            switch (src) {
                case OLED_SRC_LED:
                    OLED_Printf(0, 0, OLED_6X8, "LED TASK Running");
                    break;
                case OLED_SRC_USB:
                    OLED_Printf(0, 12, OLED_6X8, "USB TASK Running");
                    break;
                default:
                    OLED_Printf(0, 24, OLED_6X8, "UNKNOWN");
                    break;
            }
            OLED_Update();
        }
    }
}

/**
 * @brief 创建OLED任务
 *
 * OLED任务在创建后会阻塞等待 g_oled_sem；
 * 推荐其他任务使用 OLED_Trigger(src) 触发，从而携带来源信息。
 */
void OLED_TaskCreate(void) {
    xTaskCreateStatic(oled_entry, "oled", OLED_STACK_SIZE, NULL, OLED_PRIORITY, oled_stack, &oled_tcb);
}
