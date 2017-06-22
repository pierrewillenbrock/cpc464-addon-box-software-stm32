
#pragma once

#include <stdint.h>
#include <sigc++/sigc++.h>

void Timer_Setup();
/** \brief Schedules a single shot timed callback
 *
 * \param usec Time to wait for callback, in microseconds
 * \param slot Slot to be called
 * \return Handle of the timer. Will never be 0.
 */
sigc::connection Timer_Oneshot(uint32_t usec, sigc::slot<void> const &slot);
/** \brief Schedules a repeating timed callback
 *
 * The first callback happens after \p usec microseconds, the rest follows at a
 * period of \p usec microseconds.
 *
 * \param usec Interval of callbacks, in microseconds.
 * \param slot Slot to be called
 * \return Handle of the timer. Will never be 0.
 */
sigc::connection Timer_Repeating(uint32_t usec, sigc::slot<void> const &slot);
//in microseconds
uint64_t Timer_timeSincePowerOn();
