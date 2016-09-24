
#include "timer.h"
#include "irq.h"

#include <map>

struct Timer {
	uint32_t interval;
	uint32_t handle;
	Timer_Func func;
	void *data;
};

static uint64_t counter = 0;
static uint32_t handle_max = 1;
static std::multimap<uint64_t, Timer> timers;

void Timer_Setup() {
	SysTick_Config(168000); // 1 per millisecond, also enables the tick irq
}

//in microseconds
uint64_t Timer_timeSincePowerOn() {
	uint64_t ctr1, ctr2;
	volatile uint64_t * const ctrp = (volatile uint64_t * const)&counter;
	uint32_t v1, v2;
	uint32_t icsr1;
	ctr1 = *ctrp;                                //A1
	v1 = SysTick->VAL;                           //A2
	icsr1 = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;  //A3

	ctr2 = *ctrp;                                //A4
	v2 = SysTick->VAL;                           //A5

	//ISR blocked, but wraparound before A3
	if (icsr1)
		return ctr2 + 1000 + v2/168;
	//ISR blocked, but wraparound after A3
	//ISR runs between A4 and A5
	else if (ctr1 == ctr2 && v1 > v2)
		return ctr1 + v1/168;
	//ISR does not run, no wraparound
	//ISR runs between A1 and A4
	//ISR runs between A5 and A6
	else
		return ctr2 + v2/168;
}

uint32_t Timer_Oneshot(uint32_t usec, void (*func)(void* data), void* data) {
	Timer t = { 0, 0, func, data };
	uint64_t time = Timer_timeSincePowerOn() + usec;
	{
		ISR_Guard isrguard;
		t.handle = handle_max++;
		timers.insert(std::make_pair(time,t));
	}
	return t.handle;
}

void Timer_Cancel(uint32_t handle) {
	ISR_Guard isrguard;
	for(auto it = timers.begin(); it != timers.end(); it++) {
		if (it->second.handle == handle) {
			timers.erase(it);
			break;
		}
	}
}

void SysTick_Handler() {
	counter += 1000;
	while(1) {
		std::pair<uint64_t,Timer> d;
		{
			ISR_Guard isrguard;
			auto it = timers.begin();
			if (it == timers.end() ||
			    it->first > counter)
				break;
			d = *it;
			timers.erase(it);
			if (d.second.interval != 0) {
				d.first += d.second.interval;
				timers.insert(d);
			}
		}
		d.second.func(d.second.data);
	}
}
