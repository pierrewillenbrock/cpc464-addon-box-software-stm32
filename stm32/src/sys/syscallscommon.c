
#include <stdlib.h>
#include <reent.h>
#include <errno.h>
#include "irq.h"
#include <sys/info.h>

/* "malloc clue function" */

/**** Locally used variables. ****/
extern char _bheap[];           /* _bheap is set in the linker command  */
                                /* file and is the end of statically    */
                                /* allocated data (thus start of heap). */
extern char _eheap[];           /* from linker as well, should be set   */
                                /* to the lowest imaginable stack       */
                                /* address. The stack may grow beyond   */
                                /* this, but the heap will not.         */
static char *heap_ptr;          /* Points to current end of the heap.   */

/************************** _sbrk_r *************************************/
/*  Support function.  Adjusts end of heap to provide more memory to    */
/* memory allocator. Simple and dumb with no sanity checks.             */
/*  struct _reent *r    -- re-entrancy structure, used by newlib to     */
/*                      support multiple threads of operation.          */
/*  ptrdiff_t nbytes    -- number of bytes to add.                      */
/*  Returns pointer to start of new heap area.                          */
/*  Note:  This implementation is not thread safe (despite taking a     */
/* _reent structure as a parameter).                                    */
/*  Since _s_r is not used in the current implementation, the following */
/* messages must be suppressed.                                         */

void * _sbrk_r(
	struct _reent *_s_r,
	ptrdiff_t nbytes)
{
	(void)_s_r;
        char  *base;            /*  errno should be set to  ENOMEM on error     */

        if (!heap_ptr) {        /*  Initialize if first time through.           */
                heap_ptr = _bheap;
        }
        base = heap_ptr;        /*  Point to end of heap.                       */
        if (heap_ptr + nbytes > _eheap) {
                errno = ENOMEM;
                return (void *)(-1);
        }

        heap_ptr += nbytes;     /*  Increase heap.                              */

        return base;            /*  Return pointer to start of new heap area.   */
}

struct SysMemInfo sysmeminfo() {
	struct SysMemInfo info;
	info.total = _eheap - _bheap;
	info.alloc = heap_ptr - _bheap;
	info.free = _eheap - heap_ptr;
	return info;
}

int _isatty_r(struct _reent *ptr, int file)
{
	(void)ptr;(void)file;
        return 0;
}


int _getpid_r ( struct _reent *ptr )
{
	(void)ptr;
	return 1;/* this is the only process and its pid is 1. */
}

int _kill_r ( struct _reent *ptr, int a, int b )
{
	(void)ptr;(void)a;(void)b;
	errno = EPERM;
	return -1;
}

void _exit_r (int n) {
	(void)n;
label:  goto label; /* endless loop */
}

void _exit (int n) {
	(void)n;
label:  goto label; /* endless loop */
}

void*   __dso_handle = (void*) &__dso_handle;


void __cxa_pure_virtual(void) {
label:  goto label; /* endless loop */
}

static int __malloc_lock_irqsave;

void __malloc_lock(struct _reent *r) {
	(void)r;
	ISR_Disable(__malloc_lock_irqsave);
}

void __malloc_unlock(struct _reent *r) {
	(void)r;
	ISR_Enable(__malloc_lock_irqsave);
}

