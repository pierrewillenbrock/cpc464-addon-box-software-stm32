
#pragma once

#include <vector>
#include <sigc++/sigc++.h>
#include <refcounted.hpp>

struct MouseEvent {
	uint8_t buttons;//we support 8, first is at bit 0
	int8_t delta_axis[3];//we support 3 relative axis
};

class Mouse : public virtual Refcounted<Mouse> {
protected:
	sigc::signal<void,MouseEvent> m_onMouseChange;
public:
	virtual ~Mouse() {}
	virtual std::string getName() = 0;
	sigc::signal<void,MouseEvent> &onMouseChange() {
		return m_onMouseChange;
	}
	virtual void setExclusive(bool exclusive) = 0;
};

void Mouse_Setup();
std::vector<RefPtr<Mouse> > Mouse_get();
