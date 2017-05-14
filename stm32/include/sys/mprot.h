
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define MPROT_ENABLE 1
//#undef MPROT_ENABLE
#undef MPROT_LINEAR_LIST
#define MPROT_HASH

struct MProtInfo {
#ifdef MPROT_ENABLE
#ifdef MPROT_LINEAR_LIST
#endif
#ifdef MPROT_HASH
	unsigned buckets_used;
	unsigned buckets_used_by_owner;
	unsigned max_list_depth;
	unsigned average_list_depth;
#endif
#endif
};

struct MProtInfo mprot_info();

#ifdef __cplusplus
}
#endif
