#include "task_shared.h"

/* Start任务配置 */
#define START_STACK_SIZE 512 /* 初始化需要较大栈空间 */
#define START_PRIORITY   4

/* ===================== 共享RTOS对象定义 ===================== */
/* OLED任务共享信号量 */
SemaphoreHandle_t g_oled_sem = NULL;
static StaticSemaphore_t oled_sem_buf;

static StackType_t start_stack[START_STACK_SIZE];  // 任务栈
static StaticTask_t start_tcb;                     // 任务控制块

static void Task_CreateSemaphore(void) {
    /* 创建共享信号量：OLED任务收到信号后才执行一次显示 */
    g_oled_sem = xSemaphoreCreateBinaryStatic(&oled_sem_buf);
}

/**
 * @brief 启动任务入口
 * 
 * 执行系统初始化并创建工作任务，完成后自我删除
 */
static void start_entry(void* arg) {
    Task_CreateSemaphore();
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
