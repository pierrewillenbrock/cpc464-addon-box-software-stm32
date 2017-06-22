
#include <timer.hpp>
#include <unistd.h>
#include <irq.h>
#include <bits.h>

#include <map>

struct Timer {
	uint32_t interval;
	sigc::slot<void> slot;
	Timer(sigc::slot<void> slot)
		: interval(0), slot(slot)
	{
		slot.set_parent(this, &Timer::notify);
	}
	Timer(uint32_t interval, sigc::slot<void> slot)
		: interval(interval), slot(slot)
	{
		slot.set_parent(this, &Timer::notify);
	}
	static void *notify(void* data);
};

static uint64_t counter = 0;
static std::multimap<uint64_t, Timer*> timers;

void* Timer::notify(void* data) {
	ISR_Guard isrguard;
	Timer *_this = static_cast<Timer*>(data);

	for(auto it = timers.begin(); it != timers.end(); it++) {
		if(it->second == _this) {
			timers.erase(it);
			break;
		}
	}
	delete _this;
	return nullptr;
}

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
		return ctr2 + 1000 + (167999-v2)/168;
	//ISR blocked, but wraparound after A3
	//ISR runs between A4 and A5
	else if (ctr1 == ctr2 && v1 < v2) //v1 and v2 are downcounters
		return ctr1 + (167999-v1)/168;
	//ISR does not run, no wraparound
	//ISR runs between A1 and A4
	//ISR runs between A5 and A6
	else
		return ctr2 + (167999-v2)/168;
}

sigc::connection Timer_Oneshot(uint32_t usec, sigc::slot<void> const &slot) {
	Timer *t = new Timer(slot);
	uint64_t time = Timer_timeSincePowerOn() + usec;
	{
		ISR_Guard isrguard;
		timers.insert(std::make_pair(time,t));
	}
	return sigc::connection(t->slot);
	/**
	 * \todo maybe return sigc::connection? need static slot addresses for that.
	 * currently, they are inside a Timer inside a multimap, so they probably get
	 * moved around.
	 *
	 * glibmm does this:
	 *
	 * SourceConnectionNode* const conn_node = new SourceConnectionNode(slot);
	 * const sigc::connection connection(*conn_node->get_slot());
	 *
	 * class SourceConnectionNode : public sigc::notifiable
	 *  SourceConnectionNode::SourceConnectionNode(const sigc::slot_base& slot)
	 * : slot_(slot), source_(nullptr)
	 * {
	 *   slot_.set_parent(this, &SourceConnectionNode::notify);
	 * }
	 * // static
	 * void SourceConnectionNode::notify(sigc::notifiable* data)
	 * {
	 *   SourceConnectionNode* const self = static_cast<SourceConnectionNode*>(data);
	 *
	 *   // if there is no object, this call was triggered from destroy_notify_handler(),
	 *   // because we set self->source_ to nullptr there:
	 *   if (self->source_)
	 *   {
	 *     GSource* s = self->source_;
	 *     self->source_ = nullptr;
	 *     g_source_destroy(s);
	 *
	 *     // Destroying the object triggers execution of destroy_notify_handler(),
	 *     // either immediately or later, so we leave that to do the deletion.
	 *   }
	 * }
	 * // static
	 * void
	 * SourceConnectionNode::destroy_notify_callback(sigc::notifiable* data)
	 * {
	 *   SourceConnectionNode* const self = static_cast<SourceConnectionNode*>(data);
	 *
	 *   if (self)
	 *   {
	 *     // The GLib side is disconnected now, thus the GSource* is no longer valid.
	 *     self->source_ = nullptr;
	 *
	 *     delete self;
	 *   }
	 * }
	 *
	 *
	 * So, this allocates an object for every slot. i don't really want to do that. On the other hand, the multimap is probably
	 * already doing that.
	 */
}

sigc::connection Timer_Repeating(uint32_t usec, sigc::slot<void> const &slot) {
	Timer *t = new Timer(usec, slot);
	uint64_t time = Timer_timeSincePowerOn() + usec;
	{
		ISR_Guard isrguard;
		timers.insert(std::make_pair(time,t));
	}
	return sigc::connection(t->slot);
}

static void usleep_timer(volatile uint32_t *d) {
  *d = 1;
}

int usleep(useconds_t usec) {
  volatile uint32_t d = 0;
  Timer_Oneshot(usec, sigc::bind(sigc::ptr_fun(&usleep_timer), &d));
  while(!d)
    sched_yield();
  return 0;
}

void SysTick_Handler() {
	counter += 1000;
	while(1) {
		std::pair<uint64_t,Timer*> d;
		{
			ISR_Guard isrguard;
			auto it = timers.begin();
			if (it == timers.end() ||
			    it->first > counter)
				break;
			d = *it;
			timers.erase(it);
			if (d.second->interval != 0) {
				d.first += d.second->interval;
				timers.insert(d);
			}
		}
		d.second->slot();
		if (d.second->interval == 0)
			delete d.second;
	}
}
