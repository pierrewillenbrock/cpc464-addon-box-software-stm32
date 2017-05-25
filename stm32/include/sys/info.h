
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

struct SysMemInfo {
	size_t total;
	size_t free;
	size_t alloc;
};

struct SysMemInfo sysmeminfo();

#ifdef __cplusplus
}
#endif
