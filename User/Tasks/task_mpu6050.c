#include "MPU6050.h"
#include "task_shared.h"
#include "imu.h"

/* 任务优先级与周期（可按需修改） */
#define MPU6050_TASK_PRIO      (tskIDLE_PRIORITY + 3)
#define MPU6050_TASK_PERIOD_MS (3U) /* 300Hz */

static void MPU6050_Task(void* pvParameters) {
    (void)pvParameters;

    /* 初始化MPU6050（驱动内部会启动I2C DMA连续采样） */
    while (MPU6050_Init() != HAL_OK) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    

    for (;;) {
        /* 解析最近完成的一帧，并写入全局结构体 MPU6050
         * 注意：MPU_GetData() 内部会把数据写入 MPU6050（在MPU6050.c中通过指针映射实现）
         */
        if (g_mpu_mutex) {
            xSemaphoreTake(g_mpu_mutex, portMAX_DELAY);
        }

        (void)MPU_GetData();
		(void)GetAngle(&g_mpu6050_data, &g_angle, 0.003f);
		
        if (g_mpu_mutex) {
            xSemaphoreGive(g_mpu_mutex);
        }
		OLED_Trigger(OLED_SRC_MPU);

        vTaskDelay(pdMS_TO_TICKS(MPU6050_TASK_PERIOD_MS));
    }
}

void MPU6050_TaskCreate(void) {
    xTaskCreate(
        MPU6050_Task,
        "MPU6050",
        configMINIMAL_STACK_SIZE * 4,
        NULL,
        MPU6050_TASK_PRIO,
        NULL);
}
