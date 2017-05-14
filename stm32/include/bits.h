
#pragma once

#include <sys/types.h>
#include <stdint.h>

struct ARMv7M_ExceptionFrame{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t pc;
	uint32_t xpsr;
};

struct ARMv7M_FPUExceptionFrame{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t pc;
	uint32_t xpsr;
	uint32_t s[16];
	uint32_t fpscr;
	uint32_t unused;
};

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
	void (*completion)(int /*result*/, int /*errno*/, struct PReadCommand */*command*/);
	void *pdata;
};

struct PWriteCommand {
	void const *ptr;
	size_t len;
	off_t offset;
	void (*completion)(int /*result*/, int /*errno*/, struct PReadCommand */*command*/);
	void *pdata;
};

#define isRAMPtr(p) ( ((uint32_t)(p)) >= 0x20000000 &&	\
		     ((uint32_t)(p)) < 0x20000000 + 128*1024)
#define isROMPtr(p) (((uint32_t)(p)) >= 0x08000000 &&		  \
		     ((uint32_t)(p)) < 0x08000000 + 1024*1024)
#define isLOWMEMPtr(p) (((uint32_t)(p)) < 0x00000000 + 1024*1024)
#define isRWPtr(p) isRAMPtr(p)
#define isRPtr(p) ( isROMPtr(p) || isLOWMEMPtr(p) || isRAMPtr(p) )

#ifdef __cplusplus
extern "C" {
#endif

int pread_nb(int fd, struct PReadCommand *command);
int pwrite_nb(int fd, struct PWriteCommand *command);

#ifdef __cplusplus
}
#endif
