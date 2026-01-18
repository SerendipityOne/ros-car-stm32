#ifndef _KALMAN_H
#define _KALMAN_H

#include "ALL_DATA.h"

/**
 * @brief 一维卡尔曼滤波器结构体定义
 * 
 * 该结构体用于存储一维卡尔曼滤波器的各种参数和状态变量
 */
struct _1_ekf_filter {
  float LastP;  // 上一次的估计误差协方差
  float Now_P;  // 当前时刻的估计误差协方差
  float out;    // 滤波器的输出值
  float Kg;     // 卡尔曼增益
  float Q;      // 过程噪声协方差
  float R;      // 测量噪声协方差
};

extern void kalman_1(struct _1_ekf_filter* ekf, float input);  // 一维卡尔曼滤波器函数

#endif
