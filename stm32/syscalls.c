
#include <stdlib.h>
#include <reent.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

_ssize_t _read_r(struct _reent *r, int file, void *ptr, size_t len)
{
  return -1;
}

static volatile char stdout_buffer[256];

_ssize_t _write_r (
    struct _reent *r, 
    int file, 
    const void *ptr, 
    size_t len)
{
    if (len > sizeof(stdout_buffer)) {
        memcpy((void*)stdout_buffer,
	       ((char*)ptr)+len-sizeof(stdout_buffer),
	       sizeof(stdout_buffer));
        return len;
    }
    memmove((void*)stdout_buffer,
	    (void*)(stdout_buffer+sizeof(stdout_buffer)-len),
	    sizeof(stdout_buffer)-len);
    memcpy((void*)(stdout_buffer+sizeof(stdout_buffer)-len), ptr, len);
    return len;
}

int _open_r(struct _reent *r,
    const char *file, int flags, int mode )
{
  //we could implement a filesystem here. fd #0, 1, 2 are fixed and will not
  //be opened
  r->_errno = ENODEV;
  return -1;
}

int _close_r(
    struct _reent *r, 
    int file)
{
        return 0;
}

_off_t _lseek_r(
    struct _reent *r, 
    int file, 
    _off_t ptr, 
    int dir)
{
        return (_off_t)0;       /*  Always indicate we are at file beginning.   */
}


int _fstat_r(
    struct _reent *r, 
    int file, 
    struct stat *st)
{
        /*  Always set as character device.                             */
        st->st_mode = 0;/* don't set S_IFCHR, else newlib tries to test for
                           tty using isatty (not _isatty_r) */
                /* assigned to strong type with implicit        */
                /* signed/unsigned conversion.  Required by     */
                /* newlib.                                      */

        return 0;
}

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

int _isatty_r(struct _reent *ptr, int file)
{
        return 0;
}


int _getpid_r ( struct _reent *ptr )
{
   /* return "not supported" */
  return -1;
}

int _kill_r ( struct _reent *ptr, int a, int b )
{
   /* return "not supported" */
  return -1;
}

void _exit_r (int n) {
label:  goto label; /* endless loop */
}

void _exit (int n) {
label:  goto label; /* endless loop */
}

void*   __dso_handle = (void*) &__dso_handle;


void __cxa_pure_virtual(void) { 
label:  goto label; /* endless loop */
}
