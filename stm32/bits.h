
#pragma once

#include <sys/types.h>

#ifdef __cplusplus
#define container_of(ptr, type, member) ({			\
	const decltype( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#else
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

struct PReadCommand {
	void *ptr;
	size_t len;
	off_t offset;
	void (*completion)(int result, int errno, struct PReadCommand *command);
	void *pdata;
};

struct PWriteCommand {
	void const *ptr;
	size_t len;
	off_t offset;
	void (*completion)(int result, int errno, struct PReadCommand *command);
	void *pdata;
};

#ifdef __cplusplus
extern "C" {
#endif

int pread_nb(int fd, struct PReadCommand *command);
int pwrite_nb(int fd, struct PWriteCommand *command);

#ifdef __cplusplus
}
#endif
