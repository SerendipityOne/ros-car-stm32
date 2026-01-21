#include "task_shared.h"
#include "FreeRTOS.h"
#include "main.h"
#include "task.h"
#include "usb_proto.h"

/* Start任务配置 */
#define START_STACK_SIZE 512 /* 初始化需要较大栈空间 */
#define START_PRIORITY   4

static StackType_t start_stack[START_STACK_SIZE];
static StaticTask_t start_tcb;

/**
 * @brief 启动任务入口
 * 
 * 执行系统初始化并创建工作任务，完成后自我删除
 */
static void start_entry(void* arg) {
    // OLED_TaskCreate();
    LED_TaskCreate();
	UsbProto_TaskCreate(); // 启动USB

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
