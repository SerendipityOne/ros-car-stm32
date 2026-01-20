# Jetson ↔ STM32F407（USB CDC /dev/ttyACM0）通信协议（精简版 v1.0）

目标：Jetson 负责所有控制（算法/导航/调参/运动学），STM32 固化为 **电机输出 + MPU6050 采集 + 安全保护**。  
STM32：CubeMX USB CDC + FreeRTOS；电机 PWM 驱动（4 路）；MPU6050；OLED（不走协议）。

---

## 1. 物理链路与约定

- 链路：USB CDC ACM（Jetson 侧设备：`/dev/ttyACM0`）
- 数据：二进制帧（非文本）
- 字节序：Little-endian（小端）
- `int16/int32` 为二进制补码
- 推荐频率：
  - Jetson → STM32 控制帧：50 Hz（20ms）
  - STM32 → Jetson IMU：100~200 Hz（固定周期上报）
- 安全：若超过 `timeout_ms` 未收到新的控制帧，STM32 自动将 4 路电机输出置 0（滑行/刹车由固件策略决定）

---

## 2. 帧格式（必须实现）

为降低复杂度：去掉版本/序号，只保留 **帧头+类型+长度+CRC**。

```

SOF0 SOF1  MSG_ID  LEN  PAYLOAD...  CRC16_L CRC16_H
0xAA 0x55   1B     1B    LEN bytes    1B      1B

```

- `SOF0 SOF1` 固定：`0xAA 0x55`
- `MSG_ID`：消息类型
- `LEN`：payload 字节数（0~255）
- `CRC16`：CRC16-CCITT (poly=0x1021, init=0xFFFF)，覆盖范围：`MSG_ID + LEN + PAYLOAD`

> 说明：USB CDC 通常很稳定，但加 CRC 能防止偶发乱流导致“误解析”，一次写好后基本不用再改。

---

## 3. 必须的消息定义（最终定型）

### 3.1 Jetson → STM32

#### 0x01 `SET_MOTOR_PWM`：四电机 PWM 指令（核心）
payload（10 bytes）：
- `int16 pwm_m1`
- `int16 pwm_m2`
- `int16 pwm_m3`
- `int16 pwm_m4`
- `uint16 timeout_ms`

**PWM 含义（与附件 Motor.c 一致，已适配更稳的“归一化范围”）：**

- 协议里 `pwm_mX` 建议范围：`[-1000, 1000]`
- STM32 内部转换为定时器 CCR（推荐做法）：
  - `ccr = min(abs(pwm) * ARR / 1000, ARR)`
- 方向：
  - `pwm > 0`：IN1=1, IN2=0
  - `pwm < 0`：IN1=0, IN2=1
  - `pwm = 0`：IN1=0, IN2=0（滑行停止，Motor_Init 默认就是这种）

**电机顺序（与你附件一致）：**

- `m1` → TIM8_CH1（TB1_A）
- `m2` → TIM8_CH2（TB1_B）
- `m3` → TIM8_CH3（TB2_A）
- `m4` → TIM8_CH4（TB2_B）

> 如果发现实际车体“正方向不一致”，优先在 Jetson 侧改符号/交换轮序即可，避免再烧 STM32。

---

#### 0x02 `MOTOR_ENABLE`：使能/失能（必须）
payload（1 byte）：
- `uint8 enable`：0=失能（输出强制 0），1=使能（允许 SET_MOTOR_PWM 生效）

---

#### 0x03 `ESTOP`：急停（必须）
payload（1 byte）：
- `uint8 action`：1=触发急停（锁存，电机立刻 0）；0=解除急停

> 建议：急停锁存直到明确解除，防止误动作。

---

#### 0x10 `PING`：链路探测（可选但建议保留）
payload：0 bytes

---

### 3.2 STM32 → Jetson

#### 0x81 `IMU_MPU6050`：MPU6050 原始数据（必须）
payload（20 bytes）：
- `uint32 t_ms`：STM32 毫秒计数（上电后）
- `int16 ax_mg`
- `int16 ay_mg`
- `int16 az_mg`
- `int16 gx_mdps`
- `int16 gy_mdps`
- `int16 gz_mdps`
- `int16 temp_cdeg`：温度（0.01℃），没有可填 0

单位：
- 加速度：mg（1g=1000mg）
- 角速度：mdps（1dps=1000mdps）

> Jetson 用它发布 `/imu/data_raw`，后续融合/姿态估计全在 Jetson 做。

---

#### 0x82 `STATUS`：状态（必须，低频）
payload（10 bytes）：
- `uint32 t_ms`
- `uint16 vbat_mV`：电池电压（mV），没有采样就填 0
- `uint16 flags`：状态位
- `uint16 err`：错误码

flags 位定义：
- bit0：motor_enabled
- bit1：estop_latched
- bit2：cmd_timeout（超时保护中）
- bit3：imu_ok
- bit4..15：保留

err（建议最少实现）：
- 0：OK
- 1：CRC_ERROR（可选统计）
- 2：LEN_ERROR
- 3：UNKNOWN_MSG
- 4：IMU_FAIL

---

#### 0x7F `ACK`：确认（可选，建议仅对低频命令回）
payload（2 bytes）：
- `uint8 ack_msg_id`
- `uint8 result`：0=OK，非0=错误

建议：仅对 `MOTOR_ENABLE / ESTOP` 回 ACK；对高频 `SET_MOTOR_PWM` 不回 ACK（减少带宽/抖动）。

---

## 4. 交互流程（最简单可跑）

1) Jetson 打开 `/dev/ttyACM0`
2) Jetson 发送 `MOTOR_ENABLE=1`
3) Jetson 以 50Hz 发送 `SET_MOTOR_PWM(...)`（带 `timeout_ms=200`）
4) STM32 固定频率上报 `IMU_MPU6050`，低频上报 `STATUS`
5) 任意时刻 Jetson 发送 `ESTOP=1` 立即急停；解除用 `ESTOP=0`

---

