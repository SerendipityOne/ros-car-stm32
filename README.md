## ROS小车 STM32↔Jetson CDC 协议（v1.0）

### 1) 基本约定

-   **链路**：USB CDC ACM（Jetson: `/dev/ttyACM0`）
-   **字节序**：小端（Little-endian）
-   **对齐**：payload 不做结构体对齐假设（建议按字段逐个 pack/unpack）
-   **标度**：尽量用整数 + 固定比例，便于调试/跨语言

### 2) 帧格式（所有消息通用）

```
SOF0  SOF1  VER  MSG_ID  SEQ  LEN_L LEN_H  PAYLOAD...  CRC_L CRC_H
0xAA  0x55  0x01  1B     1B   1B    1B     LEN bytes   2B
```

-   **SOF**：固定 0xAA 0x55（快速找帧）
-   **VER**：协议版本（v1.0 = 0x01）
-   **MSG_ID**：消息类型
-   **SEQ**：序号（Jetson 每发一帧自增；STM32 回包可复用收到的 SEQ 便于对应）
-   **LEN**：payload 长度（0～1024 都可）
-   **CRC**：CRC16-CCITT（poly=0x1021, init=0xFFFF），覆盖范围：`VER..PAYLOAD`（不含 SOF、不含 CRC 自身）

>   这样做的好处：串口乱流里也能快速重新同步 + CRC 能抓住大多数错误。