
#pragma once

#include <stdint.h>
#include <refcounted.hpp>
#include <vector>

/** \brief Functionality for handling input devices
 */
namespace input {
	struct ControlInfo {
		int32_t logical_minimum;
		int32_t logical_maximum;
		int32_t physical_minimum;
		int32_t physical_maximum;
		int32_t unit_exponent;
		uint32_t unit;
		uint16_t flags; //bit0: relative, 1: wraps, 2: nonlinear, 3: no preferred state, 4: has null state
	};

	class Device;

	struct Report {
		uint32_t usage; //what is it we are reporting on?
		Device *device;//this is not a usable reference and only valid during inputReport.
		uint16_t flags; //bit0: relative, 1: wraps, 2: nonlinear, 3: no preferred state, 4: has null state
		uint16_t control_info_index;
		int32_t value;
	};

	class Listener : public virtual Refcounted<Listener> {
	public:
		virtual void inputReport( Report const &/*rep*/) = 0;
		//dev is only valid during the runtime of remove
		virtual void remove( Device */*dev*/) {}
	};

	class Device
	{
	private:
		std::vector<RefPtr<Listener> > listeners;
	public:
		virtual ~Device() {}
		virtual ControlInfo getControlInfo(uint16_t control_info_index) = 0;
		void addListener(RefPtr<Listener> listener);
		void reportInput( Report const &rep);
		void deviceRemove();
	};

	class DeviceListener
	{
	public:
		virtual void inputDeviceAdd( Device */*dev*/) = 0;
	};

	void AddDevice ( Device *dev);
	void registerDeviceListener ( DeviceListener *listener);
};

