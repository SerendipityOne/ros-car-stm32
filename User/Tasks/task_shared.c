#include "task_shared.h"
#include "usb_proto.h"

/* Start任务配置 */
#define START_STACK_SIZE 512 /* 初始化需要较大栈空间 */
#define START_PRIORITY   4

/* OLED事件队列配置 */
#define OLED_EVT_Q_LEN   8
#define OLED_SEM_MAX     16

/* ===================== 共享RTOS对象定义 ===================== */

/* OLED任务共享信号量（计数信号量：避免多次触发被二值信号量吞掉） */
SemaphoreHandle_t g_oled_sem = NULL;
static StaticSemaphore_t oled_sem_buf;

/* OLED事件队列（用于携带触发来源src） */
QueueHandle_t g_oled_evt_q = NULL;
static StaticQueue_t oled_evt_q_buf;
static uint8_t oled_evt_q_storage[OLED_EVT_Q_LEN * sizeof(oled_src_t)];

static StackType_t start_stack[START_STACK_SIZE];  // 任务栈
static StaticTask_t start_tcb;                     // 任务控制块

static void Task_CreateRtosObjects(void) {
    /* 1) 创建OLED事件队列（先建队列，触发API会往里塞src） */
    g_oled_evt_q = xQueueCreateStatic(
        OLED_EVT_Q_LEN,
        sizeof(oled_src_t),
        oled_evt_q_storage,
        &oled_evt_q_buf
    );

    /* 2) 创建OLED计数信号量：一个触发对应一次take */
    g_oled_sem = xSemaphoreCreateCountingStatic(OLED_SEM_MAX, 0, &oled_sem_buf);
}

/* ===================== OLED触发API实现 ===================== */

/**
 * @brief 触发OLED刷新一次，并携带触发来源
 * @param src 触发来源
 *
 * 线程上下文调用（普通任务里调用）
 */
void OLED_Trigger(oled_src_t src) {
    if (g_oled_evt_q != NULL) {
        /* 队列满：丢弃最旧的一条，再塞入最新 */
        if (xQueueSend(g_oled_evt_q, &src, 0) != pdPASS) {
            oled_src_t dump;
            (void)xQueueReceive(g_oled_evt_q, &dump, 0);
            (void)xQueueSend(g_oled_evt_q, &src, 0);
        }
    }

    if (g_oled_sem != NULL) {
        (void)xSemaphoreGive(g_oled_sem);
    }
}

/**
 * @brief ISR中触发OLED刷新一次，并携带触发来源
 * @param src 触发来源
 * @param pxHigherPriorityTaskWoken 传入给FreeRTOS的woken指针
 */
void OLED_TriggerFromISR(oled_src_t src, BaseType_t *pxHigherPriorityTaskWoken) {
    if (g_oled_evt_q != NULL) {
        if (xQueueSendFromISR(g_oled_evt_q, &src, pxHigherPriorityTaskWoken) != pdPASS) {
            oled_src_t dump;
            (void)xQueueReceiveFromISR(g_oled_evt_q, &dump, pxHigherPriorityTaskWoken);
            (void)xQueueSendFromISR(g_oled_evt_q, &src, pxHigherPriorityTaskWoken);
        }
    }

    if (g_oled_sem != NULL) {
        (void)xSemaphoreGiveFromISR(g_oled_sem, pxHigherPriorityTaskWoken);
    }
}

/**
 * @brief 启动任务入口
 *
 * 执行系统初始化并创建工作任务，完成后自我删除
 */
static void start_entry(void* arg) {
    (void)arg;

    Task_CreateRtosObjects();

    OLED_TaskCreate();
    LED_TaskCreate();
    UsbProto_TaskCreate();  // 启动USB

    vTaskDelete(NULL);
}

/**
 * @brief 创建启动任务
 *
 * 在调度器启动前调用，创建唯一的启动任务
 */
void START_TaskCreate(void) {
    xTaskCreateStatic(start_entry, "start", START_STACK_SIZE, NULL, START_PRIORITY, start_stack, &start_tcb);
}
