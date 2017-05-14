
#pragma once

#include <stdint.h>
#include <refcounted.hpp>
#include <vector>

struct InputControlInfo {
	int32_t logical_minimum;
	int32_t logical_maximum;
	int32_t physical_minimum;
	int32_t physical_maximum;
	int32_t unit_exponent;
	uint32_t unit;
	uint16_t flags; //bit0: relative, 1: wraps, 2: nonlinear, 3: no preferred state, 4: has null state
};

class InputDev;

struct InputReport {
	uint32_t usage; //what is it we are reporting on?
	InputDev *device;//this is not a usable reference and only valid during inputReport.
	uint16_t flags; //bit0: relative, 1: wraps, 2: nonlinear, 3: no preferred state, 4: has null state
	uint16_t control_info_index;
	int32_t value;
};

class InputListener : public virtual Refcounted<InputListener> {
public:
	virtual void inputReport(InputReport const &/*rep*/) = 0;
	//dev is only valid during the runtime of remove
	virtual void remove(InputDev */*dev*/) {}
};

class InputDev {
private:
	std::vector<RefPtr<InputListener> > listeners;
public:
	virtual InputControlInfo getControlInfo(uint16_t control_info_index) = 0;
	void addListener(RefPtr<InputListener> listener);
	void reportInput(InputReport const &rep);
	void deviceRemove();
};

class InputDevListener {
public:
	virtual void inputDeviceAdd(InputDev */*dev*/) = 0;
};

void Input_deviceAdd(InputDev *dev);
void Input_registerDeviceListener(InputDevListener *listener);
