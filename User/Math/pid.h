#ifndef __PID_H
#define __PID_H

#include "ALL_DATA.h"

extern void CascadePID(pid_t* pidRate, pid_t* pidAngE, const float dt);  //串级PID
extern void pidRest(pid_t** pid, const uint8_t len);                         //pid数据复位
extern void pidUpdate(pid_t* pid, const float dt);                           //PID

#endif
