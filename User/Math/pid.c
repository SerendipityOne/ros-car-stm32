#include "pid.h"
#include "my_math.h"

/**
 * @brief 重置PID控制器数组中的所有控制器状态
 * 
 * 该函数将指定数量的PID控制器结构体中的积分项、前次误差、
 * 输出值和偏移量都清零，用于初始化或重置PID控制器状态
 * 
 * @param pid 指向PID控制器对象指针数组的指针
 * @param len PID控制器数组的长度，表示要重置的控制器数量
 * @return 无返回值
 */
void pidRest(pid_t** pid, const uint8_t len) {
    uint8_t i;
    for (i = 0; i < len; i++) {
        pid[i]->integ = 0;
        pid[i]->prevError = 0;
        pid[i]->out = 0;
        pid[i]->offset = 0;
    }
}

/**
 * @brief 更新PID控制器输出
 * @param pid PID控制器结构体指针，包含控制器参数和状态变量
 * @param dt 控制周期时间间隔，单位为秒
 * 
 * 该函数根据当前测量值与期望值的误差，计算PID控制器的输出。
 * 包括比例、积分、微分三个环节的计算，并更新相关状态变量。
 */
void pidUpdate(pid_t* pid, const float dt) {
    float error;
    float deriv;

    error = pid->desired - pid->measured;  //当前角度与实际角度的误差

    pid->integ += error * dt;  //误差积分累加值

    //  pid->integ = LIMIT(pid->integ,pid->IntegLimitLow,pid->IntegLimitHigh); //进行积分限幅

    deriv = (error - pid->prevError) / dt;  //前后两次误差做微分

    pid->out = pid->kp * error + pid->ki * pid->integ + pid->kd * deriv;  //PID输出

    //pid->out = LIMIT(pid->out,pid->OutLimitLow,pid->OutLimitHigh); //输出限幅

    pid->prevError = error;  //更新上次的误差
}

/**
 * 串级PID控制函数
 * 该函数实现两级PID控制器的串级控制，先计算外环角度控制器，
 * 然后将外环输出作为内环角速度控制器的期望值，最后计算内环控制器
 *
 * @param pidRate 指向内环PID控制器对象的指针（角速度环）
 * @param pidAngE 指向外环PID控制器对象的指针（角度环）
 * @param dt 控制周期时间间隔
 */
void CascadePID(pid_t* pidRate, pid_t* pidAngE, const float dt)  //串级PID
{
    pidUpdate(pidAngE, dt);  //先计算外环
    pidRate->desired = pidAngE->out;
    pidUpdate(pidRate, dt);  //再计算内环
}

/*******************************END*********************************/
