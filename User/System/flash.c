#include <stdint.h>
#include <string.h>
#include "stm32f4xx_hal.h"

#define MPU_FLASH_ADDR   ((uint32_t)0x080E0000)  // F407: Sector 11 start
#define MPU_FLASH_SECTOR FLASH_SECTOR_11

void MpuOffset_Read(int16_t out[6]) {
    if (!out) return;
    const int16_t* src = (const int16_t*)MPU_FLASH_ADDR;
    memcpy(out, src, 6 * sizeof(int16_t));
}

HAL_StatusTypeDef MpuOffset_Write(const int16_t in[6]) {
    if (!in) return HAL_ERROR;

    HAL_StatusTypeDef st;
    uint32_t sector_error = 0;

    // 1) 解锁 + 清标志（推荐）
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    // 2) 擦除 Sector 11（F4 按扇区擦）
    FLASH_EraseInitTypeDef ei = {0};
    ei.TypeErase = FLASH_TYPEERASE_SECTORS;
    ei.Sector = MPU_FLASH_SECTOR;
    ei.NbSectors = 1;
    ei.VoltageRange = FLASH_VOLTAGE_RANGE_3;  // 2.7V~3.6V（3V3系统常用）

    st = HAL_FLASHEx_Erase(&ei, &sector_error);
    if (st != HAL_OK || sector_error != 0xFFFFFFFFu) {
        HAL_FLASH_Lock();
        return HAL_ERROR;
    }

    // 3) 打包 6个int16 -> 3个word（更适合F4）
    uint32_t w[3];
    w[0] = ((uint32_t)(uint16_t)in[1] << 16) | (uint16_t)in[0];
    w[1] = ((uint32_t)(uint16_t)in[3] << 16) | (uint16_t)in[2];
    w[2] = ((uint32_t)(uint16_t)in[5] << 16) | (uint16_t)in[4];

    // 4) 按Word写入（地址必须4字节对齐）
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t dst = MPU_FLASH_ADDR + i * 4u;
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst, w[i]);
        if (st != HAL_OK) {
            HAL_FLASH_Lock();
            return st;
        }
    }

    // 5) 如果你启用了Flash数据缓存，写完建议reset一下（可选）
    // __HAL_FLASH_DATA_CACHE_RESET();

    HAL_FLASH_Lock();
    return HAL_OK;
}
