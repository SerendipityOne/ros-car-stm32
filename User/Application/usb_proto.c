#include "usb_proto.h"
#include <string.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "usbd_cdc_if.h"  // CDC_Transmit_FS

// ===================== 配置 =====================
#define RX_RB_SIZE     2048
#define TX_MAX_PAYLOAD 255
#define TX_FRAME_MAX   (2 + 1 + 1 + TX_MAX_PAYLOAD + 2)  // SOF2 + msg + len + payload + crc
#define TX_QUEUE_DEPTH 16

// ===================== 环形缓冲（单生产者：USB回调；单消费者：解析任务） =====================
static uint8_t s_rb[RX_RB_SIZE];
static volatile uint16_t s_head = 0;
static volatile uint16_t s_tail = 0;

/**
 * @brief 计算环形缓冲区下一个位置的索引
 * @param v 当前索引值
 * @return 下一个索引值
 */
static inline uint16_t rb_next(uint16_t v) {
    return (uint16_t)((v + 1u) % RX_RB_SIZE);
}

/**
 * @brief 将字节推入环形缓冲区
 * @param b 要推入的字节
 * @return 1表示成功，0表示缓冲区已满
 */
static int rb_push_byte(uint8_t b) {
    uint16_t n = rb_next(s_head);
    if (n == s_tail) return 0;  // full, drop
    s_rb[s_head] = b;
    s_head = n;
    return 1;
}

/**
 * @brief 从环形缓冲区弹出字节
 * @param out 输出参数，存储弹出的字节
 * @return 1表示成功，0表示缓冲区为空
 */
static int rb_pop_byte(uint8_t* out) {
    if (s_tail == s_head) return 0;
    *out = s_rb[s_tail];
    s_tail = rb_next(s_tail);
    return 1;
}

// ===================== CRC16-CCITT (poly=0x1021, init=0xFFFF, CCITT-FALSE) =====================
/**
 * @brief 计算CRC16-CCITT校验值
 * @param data 输入的数据
 * @param len 数据长度
 * @return 计算得到的CRC16校验值
 */
static uint16_t crc16_ccitt_false(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// ===================== TX 队列 =====================
typedef struct {
    uint8_t msg_id;
    uint8_t len;
    uint8_t payload[TX_MAX_PAYLOAD];
} TxItem;

static QueueHandle_t s_txq = NULL;
static SemaphoreHandle_t s_tx_done = NULL;

static TaskHandle_t s_rx_task = NULL;
static TaskHandle_t s_tx_task = NULL;

// ===================== 业务分发（你后面接电机/急停/使能/timeout） =====================
/**
 * @brief 处理接收的帧数据
 * @param msg_id 消息ID
 * @param payload 消息负载数据
 * @param len 负载数据长度
 */
static void handle_frame(uint8_t msg_id, const uint8_t* payload, uint8_t len) {
    switch (msg_id) {
        case MSG_SET_MOTOR_PWM:
            // payload 10 bytes:
            // int16 pwm1,pwm2,pwm3,pwm4, uint16 timeout_ms
            if (len == 10) {
                // TODO: 解析后交给你的控制模块，例如：
                // Ctrl_OnSetMotorPwm(payload);
            } else {
                // 可选：回 ACK 错误
                UsbProto_SendAck(MSG_SET_MOTOR_PWM, 2 /*LEN_ERROR*/);
            }
            break;

        case MSG_MOTOR_ENABLE:
            if (len == 1) {
                // TODO: Ctrl_OnMotorEnable(payload[0]);
                UsbProto_SendAck(MSG_MOTOR_ENABLE, 0);
            } else {
                UsbProto_SendAck(MSG_MOTOR_ENABLE, 2);
            }
            break;

        case MSG_ESTOP:
            if (len == 1) {
                // TODO: Ctrl_OnEstop(payload[0]);
                UsbProto_SendAck(MSG_ESTOP, 0);
            } else {
                UsbProto_SendAck(MSG_ESTOP, 2);
            }
            break;

        case MSG_PING:
            if (len == 0) {
                // 你也可以回一个 ACK 或回 STATUS
                UsbProto_SendAck(MSG_PING, 0);
            }
            break;

        default:
            // UNKNOWN_MSG
            UsbProto_SendAck(msg_id, 3);
            break;
    }
}

// ===================== RX 解析任务（寻帧 + CRC + 分发） =====================
/**
 * @brief USB协议接收任务，处理接收的数据并解析成帧
 * @param arg 任务参数（未使用）
 */
static void UsbProto_RxTask(void* arg) {
    (void)arg;

    enum {
        ST_SOF0 = 0,
        ST_SOF1,
        ST_MSG,
        ST_LEN,
        ST_PAYLOAD,
        ST_CRC_L,
        ST_CRC_H
    } st = ST_SOF0;

    uint8_t msg_id = 0;
    uint8_t len = 0;
    uint8_t payload[TX_MAX_PAYLOAD];
    uint8_t idx = 0;
    uint16_t crc_rx = 0;

    // 用于 CRC 计算的临时缓存：MSG_ID + LEN + PAYLOAD
    uint8_t crc_buf[2 + TX_MAX_PAYLOAD];

    for (;;) {
        // 如果没数据，睡眠等通知
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t b;
        while (rb_pop_byte(&b)) {
            switch (st) {
                case ST_SOF0:
                    st = (b == USBP_SOF0) ? ST_SOF1 : ST_SOF0;
                    break;

                case ST_SOF1:
                    st = (b == USBP_SOF1) ? ST_MSG : ST_SOF0;
                    break;

                case ST_MSG:
                    msg_id = b;
                    st = ST_LEN;
                    break;

                case ST_LEN:
                    len = b;
                    if (len > TX_MAX_PAYLOAD) {  // 防御
                        st = ST_SOF0;
                        break;
                    }
                    idx = 0;
                    st = (len == 0) ? ST_CRC_L : ST_PAYLOAD;
                    break;

                case ST_PAYLOAD:
                    payload[idx++] = b;
                    if (idx >= len) st = ST_CRC_L;
                    break;

                case ST_CRC_L:
                    crc_rx = b;
                    st = ST_CRC_H;
                    break;

                case ST_CRC_H: {
                    crc_rx |= ((uint16_t)b << 8);

                    crc_buf[0] = msg_id;
                    crc_buf[1] = len;
                    if (len) memcpy(&crc_buf[2], payload, len);

                    uint16_t crc_calc = crc16_ccitt_false(crc_buf, (uint16_t)(2 + len));
                    if (crc_calc == crc_rx) {
                        handle_frame(msg_id, payload, len);
                    } else {
                        // CRC_ERROR（可选：统计/回 ACK）
                        UsbProto_SendAck(msg_id, 1);
                    }

                    st = ST_SOF0;
                } break;

                default:
                    st = ST_SOF0;
                    break;
            }
        }
    }
}

// ===================== TX 发送任务（队列出帧 + 等待发送完成） =====================
/**
 * @brief USB协议发送任务，从队列取出数据并发送
 * @param arg 任务参数（未使用）
 */
static void UsbProto_TxTask(void* arg) {
    (void)arg;

    TxItem item;
    uint8_t frame[TX_FRAME_MAX];

    for (;;) {
        if (xQueueReceive(s_txq, &item, portMAX_DELAY) != pdTRUE) continue;

        // 组帧
        frame[0] = USBP_SOF0;
        frame[1] = USBP_SOF1;
        frame[2] = item.msg_id;
        frame[3] = item.len;
        if (item.len) memcpy(&frame[4], item.payload, item.len);

        // CRC over MSG_ID+LEN+PAYLOAD
        uint16_t crc = crc16_ccitt_false(&frame[2], (uint16_t)(2 + item.len));
        frame[4 + item.len] = (uint8_t)(crc & 0xFF);
        frame[5 + item.len] = (uint8_t)(crc >> 8);

        uint16_t frame_len = (uint16_t)(6 + item.len);

        // 发送（CDC_Transmit_FS 可能 BUSY，简单重试）
        for (;;) {
            int8_t ret = CDC_Transmit_FS(frame, frame_len);
            if (ret == USBD_OK) {
                // 等待发送完成回调释放信号量（避免连续发导致 BUSY）
                xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(50));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

// ===================== 对外 API =====================

/**
 * @brief 初始化USB协议栈的OS相关组件
 * 创建队列、信号量和任务
 */
void UsbProto_OS_Init(void) {
    s_txq = xQueueCreate(TX_QUEUE_DEPTH, sizeof(TxItem));
    s_tx_done = xSemaphoreCreateBinary();
    // 初始给一次，避免第一次发送就卡等
    xSemaphoreGive(s_tx_done);

    xTaskCreate(UsbProto_RxTask, "usb_rx", 512, NULL, tskIDLE_PRIORITY + 3, &s_rx_task);
    xTaskCreate(UsbProto_TxTask, "usb_tx", 512, NULL, tskIDLE_PRIORITY + 2, &s_tx_task);
}

/**
 * @brief 从中断服务程序向USB协议栈输入接收的字节
 * @param data 接收到的数据
 * @param len 数据长度
 */
void UsbProto_RxBytesFromISR(const uint8_t* data, uint16_t len) {
    BaseType_t hpw = pdFALSE;

    for (uint16_t i = 0; i < len; i++) {
        (void)rb_push_byte(data[i]);  // 满了就丢（可加 overflow 计数）
    }

    if (s_rx_task) {
        vTaskNotifyGiveFromISR(s_rx_task, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}

/**
 * @brief USB发送完成中断回调函数
 * 由上层USB驱动在发送完成后调用
 */
void UsbProto_OnTxCompleteFromISR(void) {
    BaseType_t hpw = pdFALSE;
    if (s_tx_done) {
        xSemaphoreGiveFromISR(s_tx_done, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}

/**
 * @brief 发送USB协议帧
 * @param msg_id 消息ID
 * @param payload 消息负载数据
 * @param len 负载数据长度
 * @return 0表示成功，负数表示错误代码
 */
int UsbProto_SendFrame(uint8_t msg_id, const void* payload, uint8_t len) {
    if (!s_txq) return -1;
    if (len > TX_MAX_PAYLOAD) return -2;

    TxItem item;
    item.msg_id = msg_id;
    item.len = len;
    if (len && payload) memcpy(item.payload, payload, len);

    if (xQueueSend(s_txq, &item, 0) != pdTRUE) {
        return -3;  // queue full
    }
    return 0;
}