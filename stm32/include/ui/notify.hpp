
#pragma once

#include <string>

namespace ui {
	class Icon;
	
	void Notification_Setup();
	/** \brief Adds a notification to the notification area
	 * 
	 * This function can safely be called from interrupt context. It may
	 * allocate some memory.
	 *
	 * \param icon      Icon to be shown
	 * \param message   Message to be shown
	 */
	void Notification_Add(std::string const & message, Icon *icon = nullptr);
};
