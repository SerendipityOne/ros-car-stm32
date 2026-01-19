#ifndef __PID_H
#define __PID_H

#include "ALL_DATA.h"

extern void CascadePID(PID_t* pidRate, PID_t* pidAngE, const float dt);  //串级PID
extern void pidRest(PID_t** pid, const uint8_t len);                         //pid数据复位
extern void pidUpdate(PID_t* pid, const float dt);                           //PID

#endif
