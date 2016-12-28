
#include <input/input.hpp>
#include <vector>

static std::vector<InputListener*> listeners;

void Input_deviceAdd(InputDev *dev) {
	for(auto &l : listeners) {
		l->inputDeviceAdd(dev);
	}
}

void Input_deviceRemove(InputDev *dev) {
	for(auto &l : listeners) {
		l->inputDeviceRemove(dev);
	}
}

void Input_reportInput(InputReport const &rep) {
	for(auto &l : listeners) {
		l->inputReport(rep);
	}
}

void Input_registerListener(InputListener *listener) {
	listeners.push_back(listener);
}
