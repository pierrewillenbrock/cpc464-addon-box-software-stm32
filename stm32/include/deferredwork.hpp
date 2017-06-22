
#pragma once

#include <sigc++/slot.h>

void addDeferredWork(sigc::slot<void> const &work);
bool doDeferredWork();
