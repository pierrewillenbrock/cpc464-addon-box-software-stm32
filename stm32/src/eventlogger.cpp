
#include <eventlogger.hpp>
#include <array>
#include <timer.h>
#include <irq.h>

struct LogInfo {
  const char *info;
  uint64_t timestamp;
  void *extra;
};

std::array<LogInfo,512> eventlog;
unsigned int eventlogpos = 0;

void LogEvent(const char *info, void *extra) {
  unsigned pos;
  {
    ISR_Guard g;
    pos = eventlogpos;
    eventlogpos++;
    if (eventlogpos >= eventlog.size())
      eventlogpos = 0;
  }
  LogInfo &i = eventlog[pos];
  i.info = info;
  i.extra = extra;
  i.timestamp = Timer_timeSincePowerOn();
}

void LogEvent(const char *info) {
  LogEvent(info, NULL);
}
