
#include <input/input.hpp>
#include <vector>

static std::vector<input::DeviceListener*> listeners;

void input::AddDevice ( input::Device *dev) {
	for(auto &l : listeners) {
		l->inputDeviceAdd(dev);
	}
}

void input::registerDeviceListener ( input::DeviceListener *listener) {
	listeners.push_back(listener);
}

void input::Device::addListener(RefPtr<input::Listener> listener) {
	listeners.push_back(listener);
}

void input::Device::reportInput( input::Report const &rep) {
	for(auto &l : listeners)
		l->inputReport(rep);
}

void input::Device::deviceRemove() {
	for(auto &l : listeners)
		l->remove(this);
}
