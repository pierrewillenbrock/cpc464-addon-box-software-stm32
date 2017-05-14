
#include <input/input.hpp>
#include <vector>

static std::vector<InputDevListener*> listeners;

void Input_deviceAdd(InputDev *dev) {
	for(auto &l : listeners) {
		l->inputDeviceAdd(dev);
	}
}

void Input_registerDeviceListener(InputDevListener *listener) {
	listeners.push_back(listener);
}

void InputDev::addListener(RefPtr<InputListener> listener) {
	listeners.push_back(listener);
}

void InputDev::reportInput(InputReport const &rep) {
	for(auto &l : listeners)
		l->inputReport(rep);
}

void InputDev::deviceRemove() {
	for(auto &l : listeners)
		l->remove(this);
}
