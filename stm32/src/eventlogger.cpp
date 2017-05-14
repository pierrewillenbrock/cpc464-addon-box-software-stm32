
#include <eventlogger.hpp>
#include <array>
#include <timer.h>
#include <irq.h>

struct LogInfo {
  const char *info;
  void *extra;
  uint64_t time;
  uint32_t rep;
  uint32_t timesince;
};

std::array<LogInfo,512> eventlog;
unsigned int eventlogpos = 0;

void LogEvent(const char *info, void *extra) {
  unsigned pos;
  uint64_t now;
  {
    ISR_Guard g;
    now = Timer_timeSincePowerOn();
    pos = eventlogpos;
    unsigned ppos = pos == 0?eventlog.size()-1:pos-1;
    if (eventlog[ppos].info == info && eventlog[ppos].extra == extra) {
	eventlog[ppos].rep++;
	eventlog[ppos].timesince = now - eventlog[ppos].time;
	return;
    }
    eventlogpos++;
    if (eventlogpos >= eventlog.size())
      eventlogpos = 0;
  }

  LogInfo &i = eventlog[pos];
  i.info = info;
  i.extra = extra;
  i.time = now;
}

void LogEvent(const char *info) {
  LogEvent(info, NULL);
}
