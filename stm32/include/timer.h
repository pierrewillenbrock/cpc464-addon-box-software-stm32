
#pragma once

#include <stdint.h>

#ifdef __cplusplus
 extern "C" {
#endif

typedef void (*Timer_Func)(void *);

void Timer_Setup();
/** \brief Schedules a single shot timed callback
 *
 * \param usec Time to wait for callback, in microseconds
 * \param func Function to be called
 * \param data Data the function should be called with
 * \return Handle of the timer
 */
uint32_t Timer_Oneshot(uint32_t usec, Timer_Func func, void* data);
uint32_t Timer_Repeating(uint32_t usec, Timer_Func func, void* data);
void Timer_Cancel(uint32_t handle);
//in microseconds
uint64_t Timer_timeSincePowerOn();

#ifdef __cplusplus
 }
#endif
