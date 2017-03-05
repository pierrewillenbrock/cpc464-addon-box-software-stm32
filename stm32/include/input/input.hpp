
#pragma once

#include <stdint.h>

struct InputControlInfo {
	int32_t logical_minimum;
	int32_t logical_maximum;
	int32_t physical_minimum;
	int32_t physical_maximum;
	int32_t unit_exponent;
	uint32_t unit;
	uint16_t flags; //bit0: relative, 1: wraps, 2: nonlinear, 3: no preferred state, 4: has null state
};

class InputDev {
public:
	virtual InputControlInfo getControlInfo(uint16_t control_info_index) = 0;
};

struct InputReport {
	uint32_t usage; //what is it we are reporting on?
	InputDev *device;
	uint16_t flags; //bit0: relative, 1: wraps, 2: nonlinear, 3: no preferred state, 4: has null state
	uint16_t control_info_index;
	int32_t value;
};

class InputListener {
public:
	virtual void inputReport(InputReport const &/*rep*/) {}
	virtual void inputDeviceAdd(InputDev */*dev*/) {}
	virtual void inputDeviceRemove(InputDev */*dev*/) {}
};

void Input_deviceAdd(InputDev *dev);
void Input_deviceRemove(InputDev *dev);
void Input_reportInput(InputReport const &rep);
void Input_registerListener(InputListener *listener);
