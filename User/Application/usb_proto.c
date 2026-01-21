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
static uint8_t s_rb[RX_RB_SIZE];      // 接收环形缓冲区
static volatile uint16_t s_head = 0;  // 缓冲区头部索引
static volatile uint16_t s_tail = 0;  // 缓冲区尾部索引

// 计算下一个索引值（循环）
static inline uint16_t rb_next(uint16_t v) {
    return (uint16_t)((v + 1u) % RX_RB_SIZE);
}

// 向环形缓冲区添加一个字节
// 返回1表示成功，返回0表示缓冲区已满
static int rb_push_byte(uint8_t b) {
    uint16_t n = rb_next(s_head);
    if (n == s_tail) return 0;  // 缓冲区已满，丢弃数据
    s_rb[s_head] = b;
    s_head = n;
    return 1;
}

// 从环形缓冲区取出一个字节
// 返回1表示成功，返回0表示缓冲区为空
static int rb_pop_byte(uint8_t* out) {
    if (s_tail == s_head) return 0;  // 缓冲区为空
    *out = s_rb[s_tail];
    s_tail = rb_next(s_tail);
    return 1;
}

// ===================== CRC16-CCITT (poly=0x1021, init=0xFFFF, CCITT-FALSE) =====================
// 计算CRC16-CCITT校验值
// 参数data: 输入数据指针
// 参数len: 数据长度
// 返回值: CRC校验值
static uint16_t crc16_ccitt_false(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;  // 初始化CRC值
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;  // 将当前字节异或到CRC高位
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)                           // 如果最高位为1
                crc = (uint16_t)((crc << 1) ^ 0x1021);  // 左移一位并异或多项式
            else
                crc = (uint16_t)(crc << 1);  // 否则仅左移一位
        }
    }
    return crc;
}

// ===================== TX 队列 =====================
// 发送队列项结构体
typedef struct {
    uint8_t msg_id;                   // 消息ID
    uint8_t len;                      // 载荷长度
    uint8_t payload[TX_MAX_PAYLOAD];  // 载荷数据
} TxItem;

static QueueHandle_t s_txq = NULL;          // 发送队列句柄
static SemaphoreHandle_t s_tx_done = NULL;  // 发送完成信号量

static TaskHandle_t s_rx_task = NULL;  // 接收任务句柄
static TaskHandle_t s_tx_task = NULL;  // 发送任务句柄

// 如果在创建接收任务之前有字节到达（例如，如果UsbProto_OS_Init
// 从StartDefaultTask调用），我们标记一个待处理的唤醒，以便
// 接收任务一旦存在就可以被触发
static volatile uint8_t s_rx_wakeup_pending = 0;

// ===================== 业务分发（你后面接电机/急停/使能/timeout） =====================
// 注意：这里只负责"收到了什么"，具体控制逻辑你在别的模块实现并在这里调用即可
// 处理接收到的数据帧
// 参数msg_id: 消息ID
// 参数payload: 载荷数据
// 参数len: 载荷长度
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
// USB协议接收任务，负责寻找帧头、校验CRC和分发消息
static void UsbProto_RxTask(void* arg) {
    (void)arg;

    enum {
        ST_SOF0 = 0,  // 状态：帧起始字节0
        ST_SOF1,      // 状态：帧起始字节1
        ST_MSG,       // 状态：消息ID
        ST_LEN,       // 状态：长度字节
        ST_PAYLOAD,   // 状态：载荷数据
        ST_CRC_L,     // 状态：CRC低位
        ST_CRC_H      // 状态：CRC高位
    } st = ST_SOF0;

    uint8_t msg_id = 0;               // 存储消息ID
    uint8_t len = 0;                  // 存储载荷长度
    uint8_t payload[TX_MAX_PAYLOAD];  // 存储载荷数据
    uint8_t idx = 0;                  // 当前载荷索引
    uint16_t crc_rx = 0;              // 接收到的CRC值

    // 用于 CRC 计算的临时缓存：MSG_ID + LEN + PAYLOAD
    uint8_t crc_buf[2 + TX_MAX_PAYLOAD];

    for (;;) {
        // 休眠直到被通知，或者定期唤醒以处理在任务创建之前可能到达的数据
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));

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
                    if (len > TX_MAX_PAYLOAD) {  // 防御性检查，防止长度超出最大值
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
                    crc_rx |= ((uint16_t)b << 8);  // 组合CRC高低位

                    crc_buf[0] = msg_id;  // 准备CRC校验数据
                    crc_buf[1] = len;
                    if (len) memcpy(&crc_buf[2], payload, len);

                    uint16_t crc_calc = crc16_ccitt_false(crc_buf, (uint16_t)(2 + len));
                    if (crc_calc == crc_rx) {
                        // CRC校验通过，处理数据帧
                        handle_frame(msg_id, payload, len);
                    } else {
                        // CRC校验失败（可选：统计/回ACK）
                        UsbProto_SendAck(msg_id, 1);
                    }

                    st = ST_SOF0;  // 重置状态，准备下一帧
                } break;

                default:
                    st = ST_SOF0;
                    break;
            }
        }
    }
}

// ===================== TX 发送任务（队列出帧 + 等待发送完成） =====================
// USB协议发送任务，负责从队列获取数据、组帧并发送
static void UsbProto_TxTask(void* arg) {
    (void)arg;

    TxItem item;                  // 发送项
    uint8_t frame[TX_FRAME_MAX];  // 存储待发送的帧

    for (;;) {
        // 从发送队列接收数据，阻塞等待直到有数据可用
        if (xQueueReceive(s_txq, &item, portMAX_DELAY) != pdTRUE) continue;

        // 组帧：SOFO + SOF1 + 消息ID + 长度 + 载荷 + CRC
        frame[0] = USBP_SOF0;                                     // 帧起始标志0
        frame[1] = USBP_SOF1;                                     // 帧起始标志1
        frame[2] = item.msg_id;                                   // 消息ID
        frame[3] = item.len;                                      // 载荷长度
        if (item.len) memcpy(&frame[4], item.payload, item.len);  // 载荷数据

        // 计算CRC校验值（对消息ID+长度+载荷进行校验）
        uint16_t crc = crc16_ccitt_false(&frame[2], (uint16_t)(2 + item.len));
        frame[4 + item.len] = (uint8_t)(crc & 0xFF);  // CRC低位
        frame[5 + item.len] = (uint8_t)(crc >> 8);    // CRC高位

        uint16_t frame_len = (uint16_t)(6 + item.len);  // 计算帧总长度

        // 发送数据（CDC_Transmit_FS 可能BUSY，简单重试）
        for (;;) {
            int8_t ret = CDC_Transmit_FS(frame, frame_len);
            if (ret == USBD_OK) {
                // 等待发送完成回调释放信号量（避免连续发送导致BUSY）
                // 如果CDC_TransmitCplt_FS正确连接，这应该很快返回
                // 如果超时，我们仍然继续，但可能会看到更多USBD_BUSY重试
                (void)xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(100));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));  // 短暂延时后重试
        }
    }
}

// ===================== 对外 API =====================
// 初始化USB协议相关的OS对象（队列、信号量和任务）
void UsbProto_TaskCreate(void) {
    // 创建发送队列
    s_txq = xQueueCreate(TX_QUEUE_DEPTH, sizeof(TxItem));
    // 创建发送完成信号量
    s_tx_done = xSemaphoreCreateBinary();
    // 开始时为空：发送完成回调将给出此信号量

    // 创建接收和发送任务
    xTaskCreate(UsbProto_RxTask, "usb_rx", 512, NULL, tskIDLE_PRIORITY + 3, &s_rx_task);
    xTaskCreate(UsbProto_TxTask, "usb_tx", 512, NULL, tskIDLE_PRIORITY + 2, &s_tx_task);

    // 如果在创建任务之前有字节到达，则触发一次接收任务
    if (s_rx_wakeup_pending || (s_head != s_tail)) {
        s_rx_wakeup_pending = 0;
        xTaskNotifyGive(s_rx_task);
    }
}

// 从USB中断接收字节数据
// 参数data: 接收到的数据指针
// 参数len: 数据长度
void UsbProto_RxBytesFromISR(const uint8_t* data, uint16_t len) {
    BaseType_t hpw = pdFALSE;

    // 将接收到的所有字节放入环形缓冲区
    for (uint16_t i = 0; i < len; i++) {
        (void)rb_push_byte(data[i]);  // 如果满了就丢弃（可以添加overflow计数）
    }

    if (s_rx_task) {
        // 通知接收任务有新数据
        vTaskNotifyGiveFromISR(s_rx_task, &hpw);
        portYIELD_FROM_ISR(hpw);
    } else {
        // 如果接收任务尚未创建，标记待处理的唤醒
        s_rx_wakeup_pending = 1;
    }
}

// USB发送完成中断回调函数
void UsbProto_OnTxCompleteFromISR(void) {
    BaseType_t hpw = pdFALSE;
    if (s_tx_done) {
        // 释放发送完成信号量，允许继续发送下一帧
        xSemaphoreGiveFromISR(s_tx_done, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}

// 发送帧函数
// 参数msg_id: 消息ID
// 参数payload: 载荷数据
// 参数len: 载荷长度
// 返回值: 0表示成功，负值表示错误
int UsbProto_SendFrame(uint8_t msg_id, const void* payload, uint8_t len) {
    if (!s_txq) return -1;                // 发送队列未初始化
    if (len > TX_MAX_PAYLOAD) return -2;  // 载荷长度超出限制

    TxItem item;
    item.msg_id = msg_id;
    item.len = len;
    if (len && payload) memcpy(item.payload, payload, len);

    // 将发送项加入队列
    if (xQueueSend(s_txq, &item, 0) != pdTRUE) {
        return -3;  // 队列已满
    }
    return 0;
}