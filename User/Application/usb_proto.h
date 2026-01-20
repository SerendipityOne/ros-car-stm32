#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ====== 协议常量 ======
#define USBP_SOF0         0xAA
#define USBP_SOF1         0x55

// Jetson -> STM32
#define MSG_SET_MOTOR_PWM 0x01
#define MSG_MOTOR_ENABLE  0x02
#define MSG_ESTOP         0x03
#define MSG_PING          0x10

// STM32 -> Jetson
#define MSG_IMU_MPU6050   0x81
#define MSG_STATUS        0x82
#define MSG_ACK           0x7F

// ====== 对外接口 ======
// 在 FreeRTOS 启动时调用（创建队列/信号量/任务）
void UsbProto_OS_Init(void);

// CDC 接收回调里调用（ISR/回调上下文）
void UsbProto_RxBytesFromISR(const uint8_t* data, uint16_t len);

// CDC 发送完成回调里调用
void UsbProto_OnTxCompleteFromISR(void);

// 业务侧：发送一帧（任务上下文调用）
int UsbProto_SendFrame(uint8_t msg_id, const void* payload, uint8_t len);

// 业务侧：可选快速 ACK
static inline int UsbProto_SendAck(uint8_t ack_msg_id, uint8_t result) {
    uint8_t p[2] = {ack_msg_id, result};
    return UsbProto_SendFrame(MSG_ACK, p, 2);
}

#ifdef __cplusplus
}
#endif
