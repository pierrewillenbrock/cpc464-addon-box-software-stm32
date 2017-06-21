
#pragma once

#include <stdint.h>
#include <sigc++/sigc++.h>

void Timer_Setup();
/** \brief Schedules a single shot timed callback
 *
 * \todo maybe return sigc::connection?
 * \param usec Time to wait for callback, in microseconds
 * \param func Function to be called
 * \param data Data the function should be called with
 * \return Handle of the timer. Will never be 0.
 */
uint32_t Timer_Oneshot(uint32_t usec, sigc::slot<void> const &slot);
uint32_t Timer_Repeating(uint32_t usec, sigc::slot<void> const &slot);
void Timer_Cancel(uint32_t handle);
//in microseconds
uint64_t Timer_timeSincePowerOn();
