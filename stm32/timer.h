
#pragma once

#include <stdint.h>

#ifdef __cplusplus
 extern "C" {
#endif

typedef void (*Timer_Func)(void *);

void Timer_Setup();
uint32_t Timer_Oneshot(uint32_t usec, Timer_Func func, void* data);
void Timer_Cancel(uint32_t handle);

#ifdef __cplusplus
 }
#endif
