#include "MPU6050.h"
#include "delay.h"
#include "flash.h"
#include "i2c.h"
#include "kalman.h"
#include "my_math.h"
#include "string.h"

//**************************************************************
#define MPU_I2C_TIMEOUT   100U
#define MPU_REG_ADDR_SIZE I2C_MEMADD_SIZE_8BIT
#define FRAME_LEN         14 /* ACC(6) + TEMP(2) + GYR(6) */
//**************************************************************
static int16_t MpuOffset[6] = {0};

static volatile int16_t* mpu = (int16_t*)&g_mpu6050_data;
//**************************************************************
// 双缓冲与状态
static uint8_t ImuBuf[2][FRAME_LEN];   /* 双缓冲 */
static volatile uint8_t CurIndex = 0;  /* 当前DMA写入缓冲索引 0/1 */
static volatile uint8_t HasFrame = 0;  /* 是否完成过至少一帧 */
static volatile uint32_t FrameSeq = 0; /* 完成帧计数 */
//**************************************************************
static HAL_StatusTypeDef MPU6050_WriteReg(uint8_t reg, uint8_t val) {
    return HAL_I2C_Mem_Write(&hi2c1, MPU_ADDR, reg, MPU_REG_ADDR_SIZE, &val, 1, MPU_I2C_TIMEOUT);
}

static HAL_StatusTypeDef MPU6050_ReadRegs(uint8_t reg, uint8_t* data, uint16_t len) {
    return HAL_I2C_Mem_Read(&hi2c1, MPU_ADDR, reg, MPU_REG_ADDR_SIZE, data, len, MPU_I2C_TIMEOUT);
}

/**
 * @brief I2C总线恢复函数，用于解决I2C总线被从设备锁定的问题
 * 
 * 当I2C总线上的某个从设备异常导致总线阻塞时（如SDA线被拉低），
 * 该函数通过将GPIO切换到普通输出模式，强制产生时钟脉冲和停止条件来释放总线
 */
static void I2C_BusRecover(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_I2C_DeInit(&hi2c1);

    /* Temporarily switch to GPIO to release the bus. */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin = MPU_SCL_Pin | MPU_SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    HAL_GPIO_WritePin(MPU_SCL_GPIO_Port, MPU_SCL_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MPU_SDA_GPIO_Port, MPU_SDA_Pin, GPIO_PIN_SET);
    delay_us(5);

    /* Clock out 9 pulses to free any stuck slave. */
    for (uint8_t i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(MPU_SCL_GPIO_Port, MPU_SCL_Pin, GPIO_PIN_RESET);
        delay_us(5);
        HAL_GPIO_WritePin(MPU_SCL_GPIO_Port, MPU_SCL_Pin, GPIO_PIN_SET);
        delay_us(5);
    }

    /* Send a STOP condition: SDA low -> SCL high -> SDA high. */
    HAL_GPIO_WritePin(MPU_SDA_GPIO_Port, MPU_SDA_Pin, GPIO_PIN_RESET);
    delay_us(5);
    HAL_GPIO_WritePin(MPU_SCL_GPIO_Port, MPU_SCL_Pin, GPIO_PIN_SET);
    delay_us(5);
    HAL_GPIO_WritePin(MPU_SDA_GPIO_Port, MPU_SDA_Pin, GPIO_PIN_SET);
    delay_us(5);

    HAL_I2C_Init(&hi2c1);
    delay_ms(2);
}

static void MPU_GetAccGyro(void) {
    (void)HAL_I2C_Mem_Read_DMA(&hi2c1, MPU_ADDR, MPU_ACCEL_XOUTH_REG, MPU_REG_ADDR_SIZE,
                               ImuBuf[CurIndex], FRAME_LEN);
}
//**************************************************************
/* 初始化MPU6050 */
HAL_StatusTypeDef MPU6050_Init(void) {
    HAL_StatusTypeDef res = HAL_OK;
    uint8_t who = 0;

    do {
        res = MPU6050_WriteReg(MPU_PWR_MGMT1_REG, 0x80); /* 复位唤醒，失能温度 */
        /* 采样率 = Gyro输出率 / (1 + SMPLRT_DIV) */
        res |= MPU6050_WriteReg(MPU_SAMPLE_RATE_REG, 0x02); /* ≈333 Hz */
        res |= MPU6050_WriteReg(MPU_PWR_MGMT1_REG, 0x03);   /* 时钟源 Gyro Z */
        res |= MPU6050_WriteReg(MPU_CFG_REG, 0x03);         /* DLPF 42 Hz */
        res |= MPU6050_WriteReg(MPU_GYRO_CFG_REG, 0x18);    /* ±2000 dps */
        res |= MPU6050_WriteReg(MPU_ACCEL_CFG_REG, 0x09);   /* ±4 g，LPF 5 Hz */
        if (res != HAL_OK) I2C_BusRecover();
    } while (res != HAL_OK);

    res = MPU6050_ReadRegs(MPU_WHO_AM_I, &who, 1);

    /* 从Flash读取零偏*/
    MpuOffset_Read(MpuOffset);

    /* 自动开始一次DMA采样（双缓冲） */
    if (res == HAL_OK && who == MPU_ID) {
        /* 启动连续DMA采样 */
        /* 第一次启动在索引 CurIndex=0 的缓冲区 */
        MPU_GetAccGyro();
    }

    return (res == HAL_OK && who == MPU_ID) ? HAL_OK : HAL_ERROR;
}

/* 连续采样：DMA完成回调里翻转缓冲并续传   */
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef* Hi2c) {
    if (Hi2c->Instance != I2C1) return;
    FrameSeq++;
    HasFrame = 1;
    CurIndex ^= 1U; /* 切换到另一块缓冲写入下一帧 */
    MPU_GetAccGyro();
}

/* 错误自恢复，避免总线卡死后停止采样  */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* Hi2c) {
    if (Hi2c->Instance != I2C1) return;
    HAL_I2C_DeInit(Hi2c);
    delay_ms(2);
    HAL_I2C_Init(Hi2c);
    MPU_GetAccGyro();
}

/* -------------------------------------------------------------------------- */
/* 将最近完成的一帧解析并写入全局结构体 MPU6050                                     */
/* -------------------------------------------------------------------------- */
HAL_StatusTypeDef MPU_GetData(void) {
    if (!HasFrame) return HAL_BUSY;

    /* 取“上一块”完成的缓冲（当前 CurIndex 正在DMA写入） */
    uint8_t ReadyIndex, i;
    __disable_irq();
    ReadyIndex = CurIndex ^ 1U;
    __enable_irq();

    uint8_t buf[FRAME_LEN];
    /* 快拷贝14字节到栈上，避免被DMA覆盖 */
    __disable_irq();
    memcpy(buf, ImuBuf[ReadyIndex], FRAME_LEN);
    __enable_irq();

    for (i = 0; i < 7; i++) {
        if (i < 3)  //以下对加速度做卡尔曼滤波
        {
            mpu[i] = (((int16_t)buf[i << 1] << 8) | buf[(i << 1) + 1]) - MpuOffset[i];  //整合为16bit，并减去水平静止校准值

            static struct _1_ekf_filter ekf[3] = {{0.02, 0, 0, 0, 0.001, 0.543},
                                                  {0.02, 0, 0, 0, 0.001, 0.543},
                                                  {0.02, 0, 0, 0, 0.001, 0.543}};
            kalman_1(&ekf[i], (float)mpu[i]);  //一维卡尔曼
            mpu[i] = (int16_t)ekf[i].out;

        } else if (i >= 4)  //以下对角速度做一阶低通滤波，跳过温度
        {
            mpu[i - 1] = (((int16_t)buf[i << 1] << 8) | buf[(i << 1) + 1]) - MpuOffset[i - 1];  //整合为16bit，并减去水平静止校准值
            uint8_t k = i - 4;
            const float factor = 0.15f;  //滤波因素
            static float tBuff[3];

            mpu[i - 1] = tBuff[k] = tBuff[k] * (1 - factor) + mpu[i - 1] * factor;
        }
    }

    return HAL_OK;
}

void MPU_SetOffset(void) {  //校准

    int32_t buffer[6] = {0};
    int16_t i;
    uint8_t k = 30;
    const int8_t MAX_GYRO_QUIET = 5;
    const int8_t MIN_GYRO_QUIET = -5;
    /*           wait for calm down    	                                                          */
    int16_t LastGyro[3] = {0};
    int16_t ErrorGyro[3];
    /*           set offset initial to zero    		*/

    memset(MpuOffset, 0, sizeof(MpuOffset));
    MpuOffset[2] = 8192;  //set offset from the 8192

    while (k--)  //30次静止则判定飞行器处于静止状态
    {
        do {
            delay_ms(10);
            MPU_GetData();
            for (i = 0; i < 3; i++) {
                ErrorGyro[i] = mpu[i + 3] - LastGyro[i];
                LastGyro[i] = mpu[i + 3];
            }
        } while ((ErrorGyro[0] > MAX_GYRO_QUIET) || (ErrorGyro[0] < MIN_GYRO_QUIET)  //标定静止
                 || (ErrorGyro[1] > MAX_GYRO_QUIET) || (ErrorGyro[1] < MIN_GYRO_QUIET) || (ErrorGyro[2] > MAX_GYRO_QUIET) || (ErrorGyro[2] < MIN_GYRO_QUIET));
    }

    /*           throw first 100  group data and make 256 group average as offset                    */
    for (i = 0; i < 356; i++)  //水平校准
    {
        MPU_GetData();
        if (100 <= i)  //取256组数据进行平均
        {
            uint8_t k;
            for (k = 0; k < 6; k++) {
                buffer[k] += mpu[k];
            }
        }
    }

    for (i = 0; i < 6; i++) {
        MpuOffset[i] = buffer[i] >> 8;  // 右移8位，相当于除以256，得到平均偏移值
    }
    MpuOffset_Write(MpuOffset);
}
/**************************************END OF FILE*************************************/
